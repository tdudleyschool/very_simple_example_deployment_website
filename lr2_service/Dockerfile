FROM python:3.11-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

#commented out for same reason as other
#CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8002", "--workers", "4"]

#!!!Will need to expose port
EXPOSE 8002

CMD ["python", "app.py"]