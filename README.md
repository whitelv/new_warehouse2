# Складський облік — Будівельний магазин

Апаратно-програмний комплекс автоматизації інвентаризації кріпильних виробів
з ваговим контролем та штрихкодовою ідентифікацією.

## Структура проекту

```
warehouse/
├── esp32/
│   └── esp_mfr.ino       # Прошивка ESP32
├── backend/
│   ├── main.py            # FastAPI сервер
│   ├── models.py          # Pydantic моделі
│   ├── database.py        # Підключення MongoDB
│   └── requirements.txt
├── frontend/
│   └── index.html         # Веб-інтерфейс
└── README.md
```

## Запуск

### 1. MongoDB
```bash
mongod
```

### 2. Backend
```bash
cd backend
pip install -r requirements.txt
uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

### 3. Frontend
Відкрити `frontend/index.html` у браузері.

### 4. ESP32
- Вказати IP комп'ютера у змінній `serverURL` у `esp_mfr.ino`
- Прошити через Arduino IDE

## Сценарій роботи

1. Працівник прикладає RFID картку → авторизація
2. Через веб-інтерфейс сканується штрихкод товару
3. Система отримує еталонну масу одиниці з БД
4. Порожня коробка → кнопка TARE (тарування)
5. Додаємо товар → кнопка SAVE → ESP32 відправляє дані на FastAPI
6. MongoDB оновлює залишок, записує операцію

## API Endpoints

| Метод | URL | Опис |
|-------|-----|------|
| GET | /products/ | Всі товари |
| GET | /products/barcode/{barcode} | Товар за штрихкодом |
| POST | /products/ | Додати товар |
| DELETE | /products/{barcode} | Видалити товар |
| GET | /operations/ | Історія операцій |
| POST | /operations/ | Прихід (від ESP32) |
| POST | /operations/outgoing/ | Витрата (вручну) |
| GET | /workers/ | Працівники |
| POST | /workers/ | Додати працівника |
| GET | /stats/ | Статистика дашборду |
