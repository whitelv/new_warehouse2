import copy
from datetime import datetime
from pathlib import Path
import sys

import pytest
from fastapi.testclient import TestClient

BACKEND_DIR = Path(__file__).resolve().parents[1]
if str(BACKEND_DIR) not in sys.path:
    sys.path.insert(0, str(BACKEND_DIR))

import main


class FakeInsertOneResult:
    def __init__(self, inserted_id):
        self.inserted_id = inserted_id


class FakeUpdateResult:
    def __init__(self, matched_count):
        self.matched_count = matched_count


class FakeDeleteResult:
    def __init__(self, deleted_count):
        self.deleted_count = deleted_count


class FakeAsyncCursor:
    def __init__(self, documents):
        self.documents = [copy.deepcopy(doc) for doc in documents]

    def sort(self, field, direction):
        reverse = direction == -1
        self.documents.sort(key=lambda doc: doc.get(field) or datetime.min, reverse=reverse)
        return self

    def limit(self, limit):
        self.documents = self.documents[:limit]
        return self

    def __aiter__(self):
        self._index = 0
        return self

    async def __anext__(self):
        if self._index >= len(self.documents):
            raise StopAsyncIteration
        item = self.documents[self._index]
        self._index += 1
        return copy.deepcopy(item)


class FakeCollection:
    def __init__(self):
        self.documents = []
        self.next_id = 1

    async def find_one(self, query):
        for doc in self.documents:
            if self._matches(doc, query):
                return copy.deepcopy(doc)
        return None

    def find(self, query=None):
        query = query or {}
        return FakeAsyncCursor([doc for doc in self.documents if self._matches(doc, query)])

    async def insert_one(self, document):
        doc = copy.deepcopy(document)
        doc.setdefault("_id", str(self.next_id))
        self.next_id += 1
        self.documents.append(doc)
        return FakeInsertOneResult(doc["_id"])

    async def update_one(self, query, update):
        for doc in self.documents:
            if self._matches(doc, query):
                doc.update(update.get("$set", {}))
                return FakeUpdateResult(1)
        return FakeUpdateResult(0)

    async def delete_one(self, query):
        for index, doc in enumerate(self.documents):
            if self._matches(doc, query):
                del self.documents[index]
                return FakeDeleteResult(1)
        return FakeDeleteResult(0)

    async def delete_many(self, query):
        before = len(self.documents)
        self.documents = [doc for doc in self.documents if not self._matches(doc, query)]
        return FakeDeleteResult(before - len(self.documents))

    async def count_documents(self, query):
        return len([doc for doc in self.documents if self._matches(doc, query)])

    def _matches(self, document, query):
        if not query:
            return True

        expr = query.get("$expr")
        if expr:
            left, right = expr["$lt"]
            left_value = document[left[1:]] if isinstance(left, str) and left.startswith("$") else left
            right_value = document[right[1:]] if isinstance(right, str) and right.startswith("$") else right
            if not left_value < right_value:
                return False

        for key, value in query.items():
            if key == "$expr":
                continue
            if document.get(key) != value:
                return False
        return True


@pytest.fixture()
def fake_db(monkeypatch):
    products = FakeCollection()
    operations = FakeCollection()
    workers = FakeCollection()

    monkeypatch.setattr(main, "products_col", products)
    monkeypatch.setattr(main, "operations_col", operations)
    monkeypatch.setattr(main, "workers_col", workers)

    main.current_session.update({"rfid": None, "name": None, "role": None})
    main.last_scanned_rfid["rfid"] = None
    main.register_mode["active"] = False
    main.login_mode["active"] = False
    main.oled_message.update({"line1": "", "line2": "", "line3": "", "updated": False})
    main.current_weight_value["weight"] = None
    main.confirmed_weight_value["weight"] = None
    main.weigh_mode["active"] = False
    main.weigh_pending["barcode"] = None

    return {"products": products, "operations": operations, "workers": workers}


@pytest.fixture()
def client(fake_db):
    with TestClient(main.app) as test_client:
        yield test_client
