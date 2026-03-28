const inputField = document.getElementById('numberInput');
const modelSelect = document.getElementById('modelSelect');
const submitButton = document.getElementById('submitBtn');
const outputField = document.getElementById('output');

// ✅ REPLACED: Instead of window.location.hostname, use Render env variable
// Render allows you to set environment variables per service
const predictUrl = process.env.API_URL || 'http://localhost:8080/predict';

async function sendPrediction(number, model) {
    try {
        console.log("Sending request...");

        const response = await fetch(predictUrl, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({x: number, model: model}) // ✅ FIXED
        });

        const data = await response.json();
        outputField.innerText = `Prediction: ${data.y}`; // ✅ FIXED
    } catch (err) {
        console.error(err);
        outputField.innerText = `Error: ${err.message}`;
    }
}

submitButton.addEventListener('click', () => {
    console.log("BUTTON CLICKED"); // 🔍 debug

    const number = parseFloat(inputField.value);
    const model = modelSelect.value;

    if (isNaN(number)) {
        outputField.innerText = 'Please enter a valid number';
        return;
    }

    sendPrediction(number, model);
});