const char *configHTML = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <title>Device Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no" />
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Poppins:wght@400;600&display=swap');

        * {
            box-sizing: border-box;
        }

        body {
            margin: 0;
            background: linear-gradient(135deg, #4e62bd, #440f80);
            font-family: 'Poppins', sans-serif;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            padding: 20px;
            color: #333;
        }

        .config-container {
            background:  #bdc2ff;
            padding: 40px 35px;
            border-radius: 15px;
            width: 100%;
            max-width: 520px;
            box-shadow: 0 15px 40px rgba(0, 0, 0, 0.2);
            transition: transform 0.3s ease;
        }

        .config-container:hover {
            transform: translateY(-5px);
            box-shadow: 0 25px 50px rgba(0, 0, 0, 0.3);
        }

        h1 {
            text-align: center;
            color: #6d5c9c;
            margin-bottom: 30px;
            font-weight: 600;
            letter-spacing: 1.2px;
        }

        .section-title {
            font-size: 20px;
            margin-top: 35px;
            margin-bottom: 20px;
            color: #5a4a8a;
            border-bottom: 3px solid #764ba2;
            padding-bottom: 6px;
            font-weight: 600;
        }

        .form-group {
            position: relative;
            margin-bottom: 22px;
        }

        label {
            display: block;
            margin-bottom: 8px;
            font-weight: 600;
            color:  #6d5c9c;
        }

        input[type="text"],
        input[type="password"] {
            width: 100%;
            padding: 14px 45px 14px 15px;
            font-size: 16px;
            border: 2px solid #7e5fa7;
            border-radius: 10px;
            transition: border-color 0.3s ease, box-shadow 0.3s ease;
        }

        input[type="text"]:focus,
        input[type="password"]:focus {
            border-color: #9971c2;
            outline: none;
            box-shadow: 0 0 10px rgba(118, 75, 162, 0.4);
        }

        /* Icon styles */
        .form-group svg {
            position: absolute;
            right: 15px;
            top: 55%;
            transform: translateY(-50%);
            width: 20px;
            height: 20px;
            fill: #8475d8;
            transition: fill 0.3s ease;
            pointer-events: none;
        }

        input[type="text"]:focus + svg,
        input[type="password"]:focus + svg {
            fill: #764ba2;
        }

        .show-password {
            margin-top: 8px;
            display: flex;
            align-items: center;
            user-select: none;
        }

        .show-password input[type="checkbox"] {
            margin: 0;
            width: 18px;
            height: 18px;
            cursor: pointer;
        }

        .show-password label {
            margin-left: 8px;
            font-weight: 500;
            color: #666;
            cursor: pointer;
        }

        button {
            margin-top: 30px;
            width: 100%;
            padding: 16px;
            background-color: #764ba2;
            color: #fff;
            font-size: 17px;
            font-weight: 600;
            border: none;
            border-radius: 10px;
            cursor: pointer;
            box-shadow: 0 6px 20px rgba(118, 75, 162, 0.5);
            transition: background-color 0.3s ease, box-shadow 0.3s ease;
        }

        button:hover {
            background-color: #5a357a;
            box-shadow: 0 8px 25px rgba(90, 53, 122, 0.7);
        }

        /* Responsive */
        @media (max-width: 540px) {
            .config-container {
                padding: 30px 25px;
                max-width: 90%;
            }
        }
    </style>
</head>
<body>
    <div class="config-container">
        <h1>Device Configuration</h1>
        <form action="/save-config" method="POST" autocomplete="off">
            <div class="section-title">Hardware Settings</div>
            <div class="form-group">
                <label for="hardwareId">Hardware ID</label>
                <input type="text" id="hardwareId" name="hardwareId" value="%HARDWARE_ID%" required />
                <svg viewBox="0 0 24 24" aria-hidden="true">
                    <path d="M12 2a7 7 0 0 1 7 7v3a7 7 0 0 1-14 0V9a7 7 0 0 1 7-7zm0 2a5 5 0 0 0-5 5v3a5 5 0 0 0 10 0v-3a5 5 0 0 0-5-5z"/>
                </svg>
            </div>
            <div class="form-group">
                <label for="hardwareIP">Hardware IP</label>
                <input type="text" id="hardwareIP" name="hardwareIP" value="%HARDWARE_IP%" required />
                <svg viewBox="0 0 24 24" aria-hidden="true">
                    <path d="M12 4a8 8 0 1 0 8 8 8 8 0 0 0-8-8zm0 14a6 6 0 1 1 6-6 6 6 0 0 1-6 6z"/>
                </svg>
            </div>

            <div class="section-title">Server Settings</div>
            <div class="form-group">
                <label for="serverIP">Server IP</label>
                <input type="text" id="serverIP" name="serverIP" value="%SERVER_IP%" required />
                <svg viewBox="0 0 24 24" aria-hidden="true">
                    <path d="M3 12h18M12 3v18"/>
                </svg>
            </div>

            <div class="section-title">WiFi Settings</div>
            <div class="form-group">
                <label for="ssid">SSID</label>
                <input type="text" id="ssid" name="ssid" value="%WIFI_SSID%" required />
                <svg viewBox="0 0 24 24" aria-hidden="true">
                    <path d="M12 3C7 3 2.7 6.1 1 10.5l1.5 1.5a9 9 0 0 1 19 0l1.5-1.5C21.3 6.1 17 3 12 3z"/>
                </svg>
            </div>
            <div class="form-group">
                <label for="password">WiFi Password</label>
                <input type="password" id="password" name="password" value="%WIFI_PASS%" required autocomplete="off" />
                <svg viewBox="0 0 24 24" aria-hidden="true">
                    <path d="M17 9h-1V7a4 4 0 0 0-8 0v2H7a2 2 0 0 0-2 2v7a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-7a2 2 0 0 0-2-2zm-5 6a1.5 1.5 0 1 1 0-3 1.5 1.5 0 0 1 0 3z"/>
                </svg>
                <div class="show-password">
                    <input type="checkbox" id="showPw" onclick="togglePassword()" />
                    <label for="showPw">Show Password</label>
                </div>
            </div>

            <button type="submit">Save Configuration</button>
        </form>
    </div>

    <script>
        function togglePassword() {
            const pwField = document.getElementById("password");
            pwField.type = pwField.type === "password" ? "text" : "password";
        }
    </script>
</body>
</html>
)rawliteral";
