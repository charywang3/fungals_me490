import pandas as pd
import os
import glob
import numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.preprocessing import StandardScaler

script_dir = os.path.dirname(os.path.abspath(__file__))
files = glob.glob(os.path.join(script_dir, "*.csv"))

dfs = []
for f in files:
    df = pd.read_csv(f)
    dfs.append(df)

data = pd.concat(dfs, ignore_index=True)

# 2. Clean data (remove bad rows)
data = data.dropna()

# 3. Select features
# IMPORTANT: match these names with your Arduino CSV header
X = data[["temp_c", "humidity", "gas_raw", "bvoc"]]

# Optional: log transform gas (HIGHLY recommended)
X["gas_raw"] = np.log(X["gas_raw"])

# Labels
y = data["label"]

# 4. Normalize features (important for ML)
scaler = StandardScaler()
X_scaled = scaler.fit_transform(X)

# 5. Train model
model = LogisticRegression(max_iter=1000)
model.fit(X_scaled, y)

# 6. Predict probabilities (example)
probs = model.predict_proba(X_scaled)

print("\nExample probabilities (first 5 rows):")
print(probs[:5])

# # 7. Print weights for Arduino
# print("\n=== COPY THESE INTO ARDUINO ===")

print("\nWeights:")
print(model.coef_)

print("\nBias:")
print(model.intercept_)

print("\nScaler mean:")
print(scaler.mean_)

print("\nScaler scale:")
print(scaler.scale_)