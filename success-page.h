const char* successHTML = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Success</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body {
            margin: 0;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background-color: #eef1f5;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
        }
        .success-container {
            background: #fff;
            padding: 40px;
            border-radius: 10px;
            box-shadow: 0 8px 20px rgba(0,0,0,0.08);
            text-align: center;
            max-width: 400px;
            width: 100%;
        }
        h1 {
            color: #2a4d9c;
            margin-bottom: 20px;
            font-size: 24px;
        }
        p {
            color: #444;
            font-size: 16px;
            line-height: 1.6;
        }
    </style>
</head>
<body>
    <div class="success-container">
        <h1>Configuration Saved</h1>
        <p>Your configuration has been successfully saved.<br>
        The device will now restart automatically.</p>
    </div>
</body>
</html>
)rawliteral";
