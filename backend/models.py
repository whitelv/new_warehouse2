from pydantic import BaseModel
from typing import Optional
from datetime import datetime


class Product(BaseModel):
    barcode: str
    name: str
    unit_weight: float        # еталонна маса одиниці в грамах
    min_stock: int            # мінімальний залишок (сповіщення)
    current_stock: int = 0    # поточна кількість
    tare_weight: Optional[float] = None   # вага тари (пустої коробки) в грамах


class ProductUpdate(BaseModel):
    name: Optional[str] = None
    unit_weight: Optional[float] = None
    min_stock: Optional[int] = None
    tare_weight: Optional[float] = None


class Operation(BaseModel):
    barcode: str
    quantity: int
    gross_weight: float       # маса брутто (г)
    tare_weight: float        # маса тари (г)
    worker_rfid: str
    type: str                 # "incoming" або "outgoing"
    timestamp: Optional[datetime] = None


class Worker(BaseModel):
    rfid: str
    name: str


class OutgoingOperation(BaseModel):
    barcode: str
    quantity: int
    worker_rfid: str
