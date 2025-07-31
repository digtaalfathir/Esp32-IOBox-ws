const char* configHTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>IoT Node Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: Arial, sans-serif;
        }
        body {
            background: #f0f2f5;
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        .container {
            background: white;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);
            width: 100%;
            max-width: 400px;
        }
        h1, h2 {
            color: #1d4982;
            text-align: center;
        }
        .section-title {
            margin: 30px 0 15px;
            padding-bottom: 10px;
            border-bottom: 2px solid #e0e0e0;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 8px;
            color: #333;
            font-weight: bold;
        }
        input {
            width: 100%;
            padding: 12px;
            border: 2px solid #e0e0e0;
            border-radius: 5px;
            font-size: 16px;
        }
        button {
            width: 100%;
            padding: 12px;
            background: #1d4982;
            color: white;
            border: none;
            border-radius: 5px;
            font-size: 16px;
            cursor: pointer;
            margin-top: 20px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>IoT Node Configuration</h1>
        <form action="/save-config" method="POST">
            <h2 class="section-title">Hardware Settings</h2>
            <div class="form-group">
                <label for="hardwareId">Hardware ID:</label>
                <input type="text" id="hardwareId" name="hardwareId" value="%HARDWARE_ID%" required>
            </div>
            <div class="form-group">
                <label for="hardwareIP">Hardware IP:</label>
                <input type="text" id="hardwareIP" name="hardwareIP" value="%HARDWARE_IP%" required>
            </div>

            <h2 class="section-title">Server Settings</h2>
            <div class="form-group">
                <label for="serverIP">Server IP:</label>
                <input type="text" id="serverIP" name="serverIP" value="%SERVER_IP%" required>
            </div>

            <h2 class="section-title">WiFi Settings</h2>
            <div class="form-group">
                <label for="ssid">SSID:</label>
                <input type="text" id="ssid" name="ssid" value="%WIFI_SSID%" required>
            </div>
            <div class="form-group">
                <label for="password">Password:</label>
                <input type="password" id="password" name="password" value="%WIFI_PASS%" required>
            </div>
            <button type="submit">Save Configuration</button>
        </form>
    </div>
</body>
</html>
)rawliteral";