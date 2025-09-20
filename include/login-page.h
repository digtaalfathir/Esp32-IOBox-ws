const char *loginHTML = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <title>Login - Device Access</title>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Poppins:wght@400;600&display=swap');

        * {
            box-sizing: border-box;
        }

        body {
            margin: 0;
            font-family: 'Poppins', sans-serif;
            background: linear-gradient(135deg, #4e62bd, #440f80);
            height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            color: #333;
        }

        .login-container {
            background: #bdc2ff;
            padding: 40px 35px;
            border-radius: 15px;
            box-shadow: 0 15px 40px rgba(0, 0, 0, 0.2);
            width: 100%;
            max-width: 400px;
            transition: transform 0.3s ease;
        }

        .login-container:hover {
            transform: translateY(-5px);
            box-shadow: 0 25px 50px rgba(0, 0, 0, 0.3);
        }

        h2 {
            text-align: center;
            margin-bottom: 30px;
            color: #6d5c9c;
            font-weight: 600;
            letter-spacing: 1.2px;
        }

        .form-group {
            position: relative;
            margin-bottom: 25px;
        }

        label {
            display: block;
            margin-bottom: 8px;
            font-weight: 600;
            color: #6d5c9c;
        }

        input {
            width: 100%;
            padding: 14px 45px 14px 15px;
            border: 2px solid #7e5fa7;
            border-radius: 8px;
            font-size: 16px;
            transition: border-color 0.3s ease;
        }

        input:focus {
            border-color: #9971c2;
            outline: none;
            box-shadow: 0 0 8px rgba(118, 75, 162, 0.4);
        }

        /* Icon styles */
        .form-group svg {
            position: absolute;
            right: 15px;
            top: 70%;
            transform: translateY(-50%);
            width: 20px;
            height: 20px;
            fill: #8475d8;
            transition: fill 0.3s ease;
            pointer-events: none;
        }

        input:focus + svg {
            fill: #3836b6;
        }

        .show-password {
            display: flex;
            align-items: center;
            margin-top: -15px;
            margin-bottom: 20px;
            user-select: none;
            color: #6d5c9c;
            font-weight: 500;
        }

        .show-password input[type="checkbox"] {
            margin-right: 8px;
            width: 18px;
            height: 18px;
            cursor: pointer;
        }

        button {
            width: 100%;
            padding: 15px;
            background: #764ba2;
            border: none;
            border-radius: 8px;
            color: rgb(167, 140, 189);
            font-size: 17px;
            font-weight: 600;
            cursor: pointer;
            transition: background 0.3s ease;
            box-shadow: 0 5px 15px rgba(37, 63, 180, 0.74);
        }

        button:hover {
            background: #5a357a;
            box-shadow: 0 8px 20px rgba(90, 53, 122, 0.6);
        }

        /* Responsive */
        @media (max-width: 420px) {
            .login-container {
                padding: 30px 20px;
                max-width: 90%;
            }
        }
    </style>
</head>
<body>
    <div class="login-container">
        <h2>Device Login</h2>
        <form action="/auth" method="POST" autocomplete="off">
            <div class="form-group">
                <label for="username">Username</label>
                <input id="username" name="username" type="text" required />
                <!-- User icon -->
                <svg viewBox="0 0 24 24" aria-hidden="true">
                    <path d="M12 12c2.7 0 4.8-2.1 4.8-4.8S14.7 2.4 12 2.4 7.2 4.5 7.2 7.2 9.3 12 12 12zm0 2.4c-3.2 0-9.6 1.6-9.6 4.8v2.4h19.2v-2.4c0-3.2-6.4-4.8-9.6-4.8z"/>
                </svg>
            </div>
            <div class="form-group">
                <label for="password">Password</label>
                <input id="password" name="password" type="password" required />
                <!-- Lock icon -->
                <svg viewBox="0 0 24 24" aria-hidden="true">
                    <path d="M17 9h-1V7a4 4 0 0 0-8 0v2H7a2 2 0 0 0-2 2v7a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-7a2 2 0 0 0-2-2zm-5 6a1.5 1.5 0 1 1 0-3 1.5 1.5 0 0 1 0 3z"/>
                </svg>
            </div>
            <div class="show-password">
                <input type="checkbox" id="showPw" onclick="togglePassword()" />
                <label for="showPw">Show Password</label>
            </div>
            <button type="submit">Login</button>
        </form>
    </div>

    <script>
        function togglePassword() {
            const pwField = document.getElementById("password");
            if (pwField.type === "password") {
                pwField.type = "text";
            } else {
                pwField.type = "password";
            }
        }
    </script>
</body>
</html>
)rawliteral";
