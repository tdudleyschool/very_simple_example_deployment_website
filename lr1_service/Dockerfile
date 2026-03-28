FROM python:3.11-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

#Now that we are including uvicorn directly in python script i guess we dont need this part to e as long
# CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8001", "--workers", "4"]

#!!!Will need to expose port
EXPOSE 8001

CMD ["python", "app.py"]