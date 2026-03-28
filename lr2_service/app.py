from fastapi import FastAPI
from pydantic import BaseModel
import pickle
import numpy as np

#needed for render. need uvicorn to run this as it's own host adn need os to manage port
import os
import uvicorn

app = FastAPI()

class InputData(BaseModel):
    x: float

# Load model
with open("lr2.pkl", "rb") as f:
    model = pickle.load(f)

@app.post("/predict")
def predict(data: InputData):
    X = np.array([[data.x]])
    y_pred = model.predict(X)[0]
    return {"y": float(y_pred)}

#!!! entier section to actually run this as it's own host
if __name__ == "__main__":
    # Use Render assigned PORT
    port = int(os.environ.get("PORT", 8002))
    uvicorn.run(app, host="0.0.0.0", port=port)