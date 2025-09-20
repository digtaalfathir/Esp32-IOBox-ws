const char *successHTML = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <title>Success</title>
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
            padding: 20px;
        }

        .success-container {
            background: #ffffff;
            padding: 40px 35px;
            border-radius: 15px;
            box-shadow: 0 15px 40px rgba(0, 0, 0, 0.2);
            max-width: 420px;
            width: 100%;
            text-align: center;
            animation: fadeInScale 0.6s ease forwards;
        }

        @keyframes fadeInScale {
            0% {
                opacity: 0;
                transform: scale(0.8);
            }
            100% {
                opacity: 1;
                transform: scale(1);
            }
        }

        .icon-check {
            width: 80px;
            height: 80px;
            margin: 0 auto 25px auto;
            fill: #4caf50;
            animation: popIn 0.5s ease forwards;
        }

        @keyframes popIn {
            0% {
                transform: scale(0);
                opacity: 0;
            }
            100% {
                transform: scale(1);
                opacity: 1;
            }
        }

        h1 {
            color: #6d5c9c;
            font-weight: 700;
            font-size: 28px;
            margin-bottom: 20px;
            letter-spacing: 1.1px;
        }

        p {
            color: #555;
            font-size: 17px;
            line-height: 1.6;
            margin: 0;
        }
    </style>
</head>
<body>
    <div class="success-container" role="alert" aria-live="polite">
        <svg class="icon-check" viewBox="0 0 24 24" aria-hidden="true" focusable="false">
            <path d="M9 16.2l-3.5-3.5-1.4 1.4L9 19 20.5 7.5l-1.4-1.4z"/>
        </svg>
        <h1>Configuration Saved</h1>
        <p>Your configuration has been successfully saved.<br>
        The device will now restart automatically.</p>
    </div>
</body>
</html>
)rawliteral";
