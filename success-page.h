const char* successHTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Configuration Saved</title>
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
            text-align: center;
        }
        h1 {
            color: #1d4982;
            margin-bottom: 20px;
            font-size: 24px;
        }
        .message {
            color: #666;
            line-height: 1.5;
            font-size: 16px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Configuration Saved</h1>
        <p class="message">Konfigurasi telah berhasil disimpan.<br>Perangkat akan restart secara otomatis.</p>
    </div>
</body>
</html>
)rawliteral";