import numpy as np
from sklearn.linear_model import LinearRegression
import pickle
import os

# Dummy data
X = np.arange(10).reshape(-1,1)
y1 = 2*X.flatten() + 1
y2 = 3*X.flatten() - 5

# Train models
lr1 = LinearRegression().fit(X, y1)
lr2 = LinearRegression().fit(X, y2)

# Ensure output directories exist
os.makedirs("../lr1_service", exist_ok=True)
os.makedirs("../lr2_service", exist_ok=True)

# Save models
with open("../lr1_service/lr1.pkl", "wb") as f:
    pickle.dump(lr1, f)

with open("../lr2_service/lr2.pkl", "wb") as f:
    pickle.dump(lr2, f)

print("Models saved in lr1_service/ and lr2_service/")