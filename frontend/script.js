const inputField = document.getElementById('numberInput');
const modelSelect = document.getElementById('modelSelect');
const submitButton = document.getElementById('submitBtn');
const outputField = document.getElementById('output');
const loadingOverlay = document.getElementById('loadingOverlay'); // a div overlay for loading

// ✅ Use Render env variable for load balancer URL
const backendUrl = window.API_URL || "https://load-balancer-tlqo.onrender.com";

// ---------------------------
// 1️⃣ Backend Ready Check
// ---------------------------
async function waitForBackendReady() {
    loadingOverlay.style.display = "flex";
    loadingOverlay.innerText = "Starting backend...";

    while (true) {
        try {
            const res = await fetch(`${backendUrl}/ready`);
            const data = await res.json();
            if (data.status === "ok") break;
        } catch (err) {
            console.log("Backend not ready yet, retrying...");
        }
        await new Promise(r => setTimeout(r, 2000)); // retry every 2 sec
    }

    loadingOverlay.style.display = "none";
}

// Keep-alive ping every 5 min
setInterval(async () => {
    try {
        await fetch(`${backendUrl}/ready`);
    } catch {}
}, 300_000); // 5 min

// ---------------------------
// 2️⃣ Prediction
// ---------------------------
async function sendPrediction(number, model) {
    loadingOverlay.style.display = "flex";
    loadingOverlay.innerText = "Predicting...";

    while (true) {
        try {
            const response = await fetch(`${backendUrl}/predict`, {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({x: number, model: model})
            });

            const data = await response.json();

            if (data.status === "model_loading") {
                loadingOverlay.innerText = "Model is still loading, please wait...";
                await new Promise(r => setTimeout(r, 2000));
                continue; // retry
            }

            outputField.innerText = `Prediction: ${data.y}`;
            break;
        } catch (err) {
            loadingOverlay.innerText = `Error: ${err.message}. Retrying...`;
            await new Promise(r => setTimeout(r, 2000));
        }
    }

    loadingOverlay.style.display = "none";
}

// ---------------------------
// 3️⃣ Event Listener
// ---------------------------
submitButton.addEventListener('click', () => {
    const number = parseFloat(inputField.value);
    const model = modelSelect.value;

    if (isNaN(number)) {
        outputField.innerText = 'Please enter a valid number';
        return;
    }

    sendPrediction(number, model);
});

// ---------------------------
// Initialize
// ---------------------------
waitForBackendReady();