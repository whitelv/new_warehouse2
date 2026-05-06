from datetime import datetime, timezone

from models import Operation, OutgoingOperation, Product, ProductUpdate, Worker


def test_product_defaults():
    product = Product(barcode="4820001", name="Bolt M8", unit_weight=12.5, min_stock=10)

    assert product.current_stock == 0
    assert product.tare_weight is None


def test_product_update_keeps_only_provided_values():
    update = ProductUpdate(name="Nut M8", min_stock=20)

    assert update.model_dump(exclude_none=True) == {"name": "Nut M8", "min_stock": 20}


def test_operation_models_accept_expected_payloads():
    now = datetime.now(timezone.utc)
    incoming = Operation(
        barcode="4820001",
        quantity=5,
        gross_weight=100.0,
        tare_weight=10.0,
        worker_rfid="A1B2C3",
        type="incoming",
        timestamp=now,
    )
    outgoing = OutgoingOperation(barcode="4820001", quantity=2, worker_rfid="A1B2C3")
    worker = Worker(rfid="A1B2C3", name="Test Worker")

    assert incoming.timestamp == now
    assert outgoing.quantity == 2
    assert worker.name == "Test Worker"
