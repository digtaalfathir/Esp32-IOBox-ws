const char* configHTML = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Device Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body {
            margin: 0;
            background: #eef1f5;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            padding: 20px;
        }
        .config-container {
            background: #fff;
            padding: 30px;
            border-radius: 10px;
            width: 100%;
            max-width: 500px;
            box-shadow: 0 8px 20px rgba(0,0,0,0.08);
        }
        h1 {
            text-align: center;
            color: #2a4d9c;
            margin-bottom: 20px;
        }
        .section-title {
            font-size: 18px;
            margin-top: 30px;
            margin-bottom: 15px;
            color: #333;
            border-bottom: 2px solid #e0e0e0;
            padding-bottom: 5px;
        }
        .form-group {
            margin-bottom: 18px;
        }
        label {
            font-weight: 600;
            display: block;
            margin-bottom: 6px;
            color: #444;
        }
        input {
            width: 100%;
            padding: 10px;
            font-size: 15px;
            border: 1.5px solid #ccc;
            border-radius: 6px;
        }
        button {
            margin-top: 25px;
            width: 100%;
            padding: 12px;
            background-color: #2a4d9c;
            color: #fff;
            font-size: 15px;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            transition: background 0.3s ease;
        }
        button:hover {
            background-color: #1d397a;
        }
    </style>
</head>
<body>
    <div class="config-container">
        <h1>Device Configuration</h1>
        <form action="/save-config" method="POST">
            <div class="section-title">Hardware Settings</div>
            <div class="form-group">
                <label for="hardwareId">Hardware ID</label>
                <input type="text" id="hardwareId" name="hardwareId" value="%HARDWARE_ID%" required>
            </div>
            <div class="form-group">
                <label for="hardwareIP">Hardware IP</label>
                <input type="text" id="hardwareIP" name="hardwareIP" value="%HARDWARE_IP%" required>
            </div>

            <div class="section-title">Server Settings</div>
            <div class="form-group">
                <label for="serverIP">Server IP</label>
                <input type="text" id="serverIP" name="serverIP" value="%SERVER_IP%" required>
            </div>

            <div class="section-title">WiFi Settings</div>
            <div class="form-group">
                <label for="ssid">SSID</label>
                <input type="text" id="ssid" name="ssid" value="%WIFI_SSID%" required>
            </div>
            <div class="form-group">
                <label for="password">WiFi Password</label>
                <input type="password" id="password" name="password" value="%WIFI_PASS%" required>
            </div>
            <button type="submit">Save Configuration</button>
        </form>
    </div>
</body>
</html>
)rawliteral";
