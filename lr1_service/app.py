from fastapi import FastAPI
from pydantic import BaseModel
import pickle
import numpy as np

app = FastAPI()

class InputData(BaseModel):
    x: float

# Load model
with open("lr1.pkl", "rb") as f:
    model = pickle.load(f)

@app.post("/predict")
def predict(data: InputData):
    X = np.array([[data.x]])
    y_pred = model.predict(X)[0]
    return {"y": float(y_pred)}