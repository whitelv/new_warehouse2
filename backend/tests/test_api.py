from bson import ObjectId

import main


def product_payload(**overrides):
    payload = {
        "barcode": "4820000000012",
        "name": "Bolt M8",
        "unit_weight": 12.5,
        "min_stock": 5,
        "current_stock": 10,
        "tare_weight": 50.0,
    }
    payload.update(overrides)
    return payload


def login_admin(client):
    response = client.get(f"/workers/{main.ADMIN_RFID}/")
    assert response.status_code == 200
    return response


def test_fix_id_converts_object_id_to_string():
    object_id = ObjectId()
    doc = {"_id": object_id, "name": "Bolt M8"}

    assert main.fix_id(doc) == {"_id": str(object_id), "name": "Bolt M8"}


def test_protected_endpoint_requires_authorization(client):
    response = client.get("/products/")

    assert response.status_code == 401
    assert response.json()["detail"] == "Authorization required"


def test_product_crud_endpoints(client):
    login_admin(client)

    create_response = client.post("/products/", json=product_payload())
    assert create_response.status_code == 200
    assert create_response.json() == {"message": "Product created", "barcode": "4820000000012"}

    duplicate_response = client.post("/products/", json=product_payload())
    assert duplicate_response.status_code == 400

    list_response = client.get("/products/")
    assert list_response.status_code == 200
    assert list_response.json()[0]["name"] == "Bolt M8"

    get_response = client.get("/products/barcode/4820000000012")
    assert get_response.status_code == 200
    assert get_response.json()["current_stock"] == 10

    update_response = client.patch("/products/4820000000012", json={"name": "Bolt M10", "min_stock": 3})
    assert update_response.status_code == 200
    assert update_response.json() == {"message": "Updated"}

    updated_product = client.get("/products/barcode/4820000000012").json()
    assert updated_product["name"] == "Bolt M10"
    assert updated_product["min_stock"] == 3

    delete_response = client.delete("/products/4820000000012")
    assert delete_response.status_code == 200
    assert delete_response.json() == {"message": "Deleted"}

    missing_response = client.get("/products/barcode/4820000000012")
    assert missing_response.status_code == 404


def test_product_update_validation(client):
    login_admin(client)

    assert client.patch("/products/missing", json={}).status_code == 400
    assert client.patch("/products/missing", json={"name": "No product"}).status_code == 404


def test_incoming_operation_updates_stock(client):
    login_admin(client)
    client.post("/products/", json=product_payload(current_stock=2, min_stock=5))

    response = client.post(
        "/operations/",
        json={
            "barcode": "4820000000012",
            "quantity": 4,
            "gross_weight": 110.0,
            "tare_weight": 10.0,
            "worker_rfid": "A1B2C3",
            "type": "incoming",
        },
    )

    assert response.status_code == 200
    assert response.json()["new_stock"] == 6
    assert response.json()["warning"] is None

    product = client.get("/products/barcode/4820000000012").json()
    assert product["current_stock"] == 6

    operations = client.get("/operations/").json()
    assert len(operations) == 1
    assert operations[0]["type"] == "incoming"


def test_outgoing_operation_updates_stock_and_warns(client):
    login_admin(client)
    client.post("/products/", json=product_payload(current_stock=10, min_stock=8))

    response = client.post(
        "/operations/outgoing/",
        json={"barcode": "4820000000012", "quantity": 3, "worker_rfid": "A1B2C3"},
    )

    assert response.status_code == 200
    assert response.json()["new_stock"] == 7
    assert "Low stock" in response.json()["warning"]

    product = client.get("/products/barcode/4820000000012").json()
    assert product["current_stock"] == 7


def test_outgoing_operation_rejects_missing_product_and_insufficient_stock(client):
    login_admin(client)

    missing_response = client.post(
        "/operations/outgoing/",
        json={"barcode": "missing", "quantity": 1, "worker_rfid": "A1B2C3"},
    )
    assert missing_response.status_code == 404

    client.post("/products/", json=product_payload(current_stock=1))
    insufficient_response = client.post(
        "/operations/outgoing/",
        json={"barcode": "4820000000012", "quantity": 5, "worker_rfid": "A1B2C3"},
    )
    assert insufficient_response.status_code == 400


def test_workers_session_and_admin_rules(client):
    admin_response = client.get(f"/workers/{main.ADMIN_RFID}/")
    assert admin_response.status_code == 200
    assert admin_response.json()["role"] == "admin"
    assert client.get("/session/").json()["role"] == "admin"

    logout_response = client.post("/session/logout/")
    assert logout_response.status_code == 200
    assert client.get("/session/").json() == {"rfid": None, "name": None, "role": None}

    login_admin(client)

    reserved_response = client.post("/workers/", json={"rfid": main.ADMIN_RFID, "name": "Reserved"})
    assert reserved_response.status_code == 400

    create_response = client.post("/workers/", json={"rfid": "ab12", "name": "Storekeeper"})
    assert create_response.status_code == 200

    duplicate_response = client.post("/workers/", json={"rfid": "AB12", "name": "Storekeeper"})
    assert duplicate_response.status_code == 400

    worker_response = client.get("/workers/ab12/")
    assert worker_response.status_code == 200
    assert worker_response.json()["rfid"] == "AB12"
    assert client.get("/session/").json()["role"] == "storekeeper"

    list_response = client.get("/workers/")
    assert len(list_response.json()) == 2

    login_admin(client)

    delete_response = client.delete("/workers/AB12")
    assert delete_response.status_code == 200
    assert client.get("/workers/AB12/").status_code == 404


def test_device_state_endpoints(client):
    assert client.post("/oled/", json={"line1": "Ready", "line2": "Scan", "line3": "RFID"}).json() == {"ok": True}
    assert client.get("/oled/").json() == {
        "updated": True,
        "line1": "Ready",
        "line2": "Scan",
        "line3": "RFID",
    }
    assert client.get("/oled/").json()["updated"] is False

    assert client.post("/weight/mode/", json={"active": True}).json() == {"active": True}
    assert client.get("/weight/mode/").json() == {"active": True}
    current_weight_post = client.post("/weight/current/", json={"weight": 123.4}).json()
    assert current_weight_post["ok"] is True
    assert current_weight_post["active"] is True
    assert current_weight_post["sequence"] >= 1

    current_weight_get = client.get("/weight/current/").json()
    assert current_weight_get["weight"] == 123.4
    assert current_weight_get["sequence"] == current_weight_post["sequence"]
    assert current_weight_get["updated_at"] is not None

    assert client.post("/weight/confirmed/", json={"weight": 120.0}).json() == {"ok": True}
    assert client.get("/weight/confirmed/").json() == {"weight": 120.0}
    assert client.get("/weight/confirmed/").json() == {"weight": None}

    assert client.post("/rfid/register-mode/", json={"active": True}).json() == {"active": True}
    assert client.post("/rfid/scanned/", json={"rfid": "ab12"}).json() == {
        "message": "RFID received",
        "rfid": "AB12",
        "mode": "register",
    }
    assert client.get("/rfid/last/").json() == {"rfid": "AB12"}
    assert client.get("/rfid/last/").json() == {"rfid": None}

    assert client.post("/weigh/start/", json={"barcode": "4820000000012"}).json() == {"ok": True}
    assert client.get("/weigh/pending/").json() == {"barcode": "4820000000012"}
    assert client.post("/weigh/confirm/").json() == {"ok": True}
    assert client.get("/weigh/pending/").json() == {"barcode": None}


def test_stats_and_clear_operations(client):
    login_admin(client)

    client.post("/products/", json=product_payload(barcode="low", current_stock=2, min_stock=5))
    client.post("/products/", json=product_payload(barcode="ok", current_stock=8, min_stock=5))
    client.post(
        "/operations/",
        json={
            "barcode": "ok",
            "quantity": 1,
            "gross_weight": 20.0,
            "tare_weight": 5.0,
            "worker_rfid": "A1B2C3",
            "type": "incoming",
        },
    )

    stats = client.get("/stats/").json()
    assert stats["total_products"] == 2
    assert stats["total_operations"] == 1
    assert stats["low_stock_count"] == 1
    assert stats["low_stock_items"][0]["barcode"] == "low"

    clear_response = client.delete("/operations/")
    assert clear_response.status_code == 200
    assert client.get("/stats/").json()["total_operations"] == 0
