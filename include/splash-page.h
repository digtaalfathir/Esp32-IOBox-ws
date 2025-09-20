const char *splashHTML = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <title>Welcome - IoT Node Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Poppins:wght@600&display=swap');

        * {
            box-sizing: border-box;
        }

        body, html {
            margin: 0;
            padding: 0;
            height: 100%;
            font-family: 'Poppins', sans-serif;
            background: linear-gradient(135deg, #667eea, #764ba2);
            display: flex;
            justify-content: center;
            align-items: center;
            color: white;
            overflow: hidden;
        }

        .splash-container {
            text-align: center;
            max-width: 400px;
            padding: 20px;
            animation: fadeInScale 1.2s ease forwards;
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

        h1 {
            font-size: 2.2rem;
            margin-bottom: 25px;
            letter-spacing: 2px;
            animation: slideUpFade 1.5s ease forwards;
        }

        @keyframes slideUpFade {
            0% {
                opacity: 0;
                transform: translateY(30px);
            }
            100% {
                opacity: 1;
                transform: translateY(0);
            }
        }

        .component-image {
            width: 150px;
            height: 150px;
            margin: 0 auto;
            animation: bounce 2s infinite ease-in-out;
        }

        @keyframes bounce {
            0%, 100% {
                transform: translateY(0);
            }
            50% {
                transform: translateY(-15px);
            }
        }

        /* Responsive */
        @media (max-width: 450px) {
            h1 {
                font-size: 1.6rem;
            }
            .component-image {
                width: 120px;
                height: 120px;
            }
        }
    </style>
</head>
<body>
    <div class="splash-container" role="banner" aria-label="Welcome screen">
        <h1>WELCOME<br>CONFIGURASI IOT NODE</h1>
        <!-- Contoh gambar komponen: sensor atau chip -->
        <svg class="component-image" viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" aria-hidden="true" focusable="false">
            <rect x="12" y="12" width="40" height="40" rx="6" ry="6" fill="#fff" stroke="#764ba2" stroke-width="3"/>
            <circle cx="32" cy="32" r="10" stroke="#764ba2" stroke-width="3" fill="#b39ddb"/>
            <line x1="32" y1="22" x2="32" y2="42" stroke="#764ba2" stroke-width="2"/>
            <line x1="22" y1="32" x2="42" y2="32" stroke="#764ba2" stroke-width="2"/>
        </svg>
    </div>

    <script>
        // Redirect ke halaman login setelah 4 detik
        setTimeout(() => {
            window.location.href = "/login"; // Ganti sesuai URL login Anda
        }, 4000);
    </script>
</body>
</html>
)rawliteral";
