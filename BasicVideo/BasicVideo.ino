#include <WiFi.h>
#include <WebServer.h>
#include <ESP32-RTSPServer.h>
#include "esp_camera.h"
#include <ESPmDNS.h>

// Reference: Camera pin definitions and setup adapted from MJPEG2SD project by s60sc (https://github.com/s60sc/ESP32-CAM_MJPEG2SD)
// ===================
// Select camera model
// ===================
// User's ESP32 cam board
#if defined(CONFIG_IDF_TARGET_ESP32)
//define CAMERA_MODEL_AI_THINKER 
#define CAMERA_MODEL_WROVER_KIT 
//#define CAMERA_MODEL_ESP_EYE 
//#define CAMERA_MODEL_M5STACK_PSRAM 
//#define CAMERA_MODEL_M5STACK_V2_PSRAM 
//#define CAMERA_MODEL_M5STACK_WIDE 
//#define CAMERA_MODEL_M5STACK_ESP32CAM
//#define CAMERA_MODEL_M5STACK_UNITCAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL 
//#define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_TTGO_T_CAMERA_PLUS
//#define CAMERA_MODEL_UICPAL_ESP32
//#define AUXILIARY

// User's ESP32S3 cam board
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define CAMERA_MODEL_FREENOVE_ESP32S3_CAM
//#define CAMERA_MODEL_PCBFUN_ESP32S3_CAM
//#define CAMERA_MODEL_XIAO_ESP32S3 
//#define CAMERA_MODEL_NEW_ESPS3_RE1_0
//#define CAMERA_MODEL_M5STACK_CAMS3_UNIT
//#define CAMERA_MODEL_ESP32S3_EYE 
//#define CAMERA_MODEL_ESP32S3_CAM_LCD
//#define CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3
//#define CAMERA_MODEL_DFRobot_Romeo_ESP32S3
//#define CAMERA_MODEL_XENOIONEX
//#define CAMERA_MODEL_Waveshare_ESP32_S3_ETH
//#define CAMERA_MODEL_DFRobot_ESP32_S3_AI_CAM
#endif
#include "camera_pins.h"

// ===========================
// Enter your WiFi credentials
// ===========================
const char *ssid = "The Cloud District 5G";
const char *password = "khajiits";

bool newMessage = false;
String lastMessage;


// RTSPServer instance
RTSPServer rtspServer;

// Variable to hold quality for RTSP frame
int quality;
// Task handles
TaskHandle_t videoTaskHandle = NULL; 

/** 
 * @brief Sets up the camera with the specified configuration. 
*/
// Camera setup function
bool setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 15000000;
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 10;
  config.fb_count = 2;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  // for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_QVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_QVGA;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif
  Serial.println("Camera Setup Complete");
  return true;
}





WebServer server(80);

  String html = R"rawliteral(
<html>
<head>
  <style>
  body {background-color: black;}
  <h1 style="background-color:DodgerBlue;">DodgerBlue</h1>
.name-container {
  text-align: center;
  margin-bottom: 20px;
  font-family: 'Courier New', Courier, monospace; /* Terminal font */
}

.glitch-name {
  position: relative;
  font-size: 2.5rem;
  font-weight: bold;
  color: green;
  margin: 0 15px;
  display: inline-block;
  text-transform: uppercase;
}

/* The Glitch Effect */
.glitch-name::before,
.glitch-name::after {
  content: attr(data-text);
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background: black; /* Matches your site background */
}

.glitch-name:hover::before {
  left: 2px;
  text-shadow: -2px 0 #ff00c1;
  clip: rect(44px, 450px, 56px, 0);
  animation: glitch-anim 5s infinite linear alternate-reverse;
}

.glitch-name:hover::after {
  left: -2px;
  text-shadow: -2px 0 #00fff9;
  clip: rect(44px, 450px, 56px, 0);
  animation: glitch-anim2 5s infinite linear alternate-reverse;
}

@keyframes glitch-anim {
  0% { clip: rect(31px, 9999px, 94px, 0); }
  20% { clip: rect(62px, 9999px, 42px, 0); }
  40% { clip: rect(16px, 9999px, 78px, 0); }
  60% { clip: rect(87px, 9999px, 12px, 0); }
  80% { clip: rect(23px, 9999px, 55px, 0); }
  100% { clip: rect(5px, 9999px, 81px, 0); }
}

@keyframes glitch-anim2 {
  0% { clip: rect(65px, 9999px, 100px, 0); }
  20% { clip: rect(12px, 9999px, 55px, 0); }
  /* ... repeat variation ... */
  100% { clip: rect(82px, 9999px, 18px, 0); }
}
body {color: white;}
</style>
</head>

<body>

<h2 style="text-align:center;">
  Silicon Crusaders Camera/Controls/Wireless Terminal Interface
</h2>

<div style="display:flex; justify-content:center; align-items:flex-start; gap:50px;">

  <!-- LEFT IMAGE -->
  <img src="https://live.staticflickr.com/65535/55208340867_9b2eecdff1_b.jpg"
       width="200">
	
  <!-- TERMINAL -->
  <div style="width:900px;">

    <div id="output" class="terminal"
         style="width:100%;height:300px;border:4px white;overflow:auto;"></div>

    <input id="input" type="text" style="width:50%;">
    <button onclick="sendInput()">Send</button>

  </div>

  <!-- RIGHT IMAGE -->
  <img src="https://live.staticflickr.com/65535/55208340867_9b2eecdff1_b.jpg"
       width="200">

  

  

</div>
<p>Server IP: <strong>%IP_ADDRESS%</strong></p>
visit rtsp://ip for the real time stream

<div class="name-container" style="text-align:center;">
  <span class="glitch-name" data-text="GUY1">Jeremy</span>
  <span class="glitch-name" data-text="GUY2">Martin</span>
  <span class="glitch-name" data-text="GUY3">Luke</span>
  </div>

<div class="gifs" style="text-align:center; margin: 20px;">
<img src="https://media.tenor.com/NibVoMbggJkAAAAC/eggman-robotnik.gif" 
     width="200" 
     alt="Cool Animation">

<img src="https://mysterygomez.wordpress.com/wp-content/uploads/2017/08/841439993fd1dfe207807ca2b0ca2528caf269e2_hq.gif" 
     width="200" 
     alt="Cool Animation">

<img src="https://media.tenor.com/ZMyx6aH7qvgAAAAM/aosth-robotnik.gif" 
     width="200" 
     alt="Cool Animation">

<img src="https://media1.tenor.com/m/a8FAWwmWRisAAAAC/robotnik-aosth.gif" 
     width="200" 
     alt="Cool Animation"> 
</div>

<div class="control-panel" style="text-align:left; margin: 20px;">
  camera Settings<br>
  <label for="quality">JPEG Compression:</label><br>
  <input type="range" id="quality" name="quality" min="10" max="63" value="12" 
         oninput="updateQuality(this.value)">
  <span id="qVal">12</span>

  <label for="res">Resolution:</label>
  <select id="res" onchange="updateResolution(this.value)" 
          style="background: black; color: #00fff9; border: 1px solid #ff00c1;">
    <option value="5">QVGA (320x240) - FASTEST</option>
    <option value="6">CIF (400x296)</option>
    <option value="8">VGA (640x480)</option>
    <option value="10">SVGA (800x600)</option>
    <option value="13">UXGA (1600x1200) - SLOWEST</option>
  </select>


</div>

<script>
function updateResolution(val) {
  // Send the resolution index to the ESP32
  fetch("/send?data=r" + val)
    .then(response => console.log("Resolution change requested: " + val));
}
</script>


<script>
function updateQuality(val) {
  document.getElementById("qVal").innerText = val;
  // Send the value to the ESP32 via your existing /send route
  fetch("/send?data=q" + val)
    .then(response => console.log("Quality updated to: " + val));
}
</script>

<script>



function sendInput() {
  let text = document.getElementById("input").value;

  fetch("/send?data=" + encodeURIComponent(text))       //this line gets run when button clicked
    .then(response => response.text())
    .then(data => {
      document.getElementById("output").innerHTML += "<br>> " + text;
      document.getElementById("output").innerHTML += "<br>" + data;
    });

  document.getElementById("input").value = "";
}
</script>

<script>
let lastLine = "";

setInterval(() => {
  fetch("/read")
    .then(res => res.text())
    .then(data => {
      if (data && data !== lastLine) {
        let out = document.getElementById("output");
        out.innerHTML += "<br>" + data;
        out.scrollTop = out.scrollHeight;

        lastLine = data;
      }
    });
}, 500);
</script>


</body>
</html>
)rawliteral";

void handleRoot() 
{
  

  String s = html; // Create a temporary copy of the HTML
  String ip = WiFi.localIP().toString();
  
  // Swap the placeholder for the real IP
  s.replace("%IP_ADDRESS%", ip);
  
  server.send(200, "text/html", s);
}

void handleSend() {
  if (server.hasArg("data")) {
    String input = server.arg("data");
    

    if (input.startsWith("q")) {
      int newQuality = input.substring(1).toInt(); // Grab the number after 'q'
      
      sensor_t * s = esp_camera_sensor_get();
      if (s) {
        s->set_quality(s, newQuality);
        Serial.print("Camera quality set to: ");
        Serial.println(newQuality);
      }
    }
    
    else if (input.startsWith("r")) {
        int resIndex = input.substring(1).toInt();
        sensor_t * s = esp_camera_sensor_get();
        if (s) {
         // framesize_t is an enum, so we cast the int to framesize_t
          s->set_framesize(s, (framesize_t)resIndex);
          Serial.print("Resolution updated to index: ");
          Serial.println(resIndex);
        }
    }
    else if (input == "on") {
        digitalWrite(2, LOW);
      } else if (input == "off") {
        digitalWrite(2, HIGH);
      }
      else
      {
        Serial.println(input); // Send website text to Serial Monitor
      }
    
    // ALWAYS send a response to the web page so the fetch() completes
    //server.send(200, "text/plain", ">" + input);
  }
}


void handleRead()
{
  if (newMessage) {
    newMessage = false;
    server.send(200, "text/plain", lastMessage);
    lastMessage = "";
  } else {
    server.send(200, "text/plain", "");
  }
}




/** 
 * @brief Retrieves the current frame quality from the camera. 
*/
void getFrameQuality() { 
  sensor_t * s = esp_camera_sensor_get(); 
  quality = s->status.quality; 
  Serial.printf("Camera Quality is: %d\n", quality);
}

/** 
 * @brief Task to send jpeg frames via RTP. 
*/
void sendVideo(void* pvParameters) { 
  while (true) { 
    // Send frame via RTP
    if(rtspServer.readyToSendFrame()) {
      camera_fb_t* fb = esp_camera_fb_get();
      rtspServer.sendRTSPFrame(fb->buf, fb->len, quality, fb->width, fb->height);
      esp_camera_fb_return(fb);
    }
    vTaskDelay(pdMS_TO_TICKS(2)); 
  }
}




void setup() {
  // Initialize serial communication
  //Serial.begin(9600);
  Serial.begin(9600, SERIAL_8N1, 12, 13);

  pinMode(2, OUTPUT);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");

  if (!MDNS.begin("crusaders")) { // This sets the name to 'crusader.local'
  Serial.println("Error setting up MDNS responder!");
  } else {
  Serial.println("mDNS responder started: http://crusaders.local");
  }

  server.on("/", handleRoot);
  server.on("/send", handleSend);
  server.on("/read", handleRead);
  server.begin();
  // Set ESP32 core debug level to Info for verbose logging
  esp_log_level_set("*", ESP_LOG_INFO);


  // Setup camera
  if (!setupCamera()) {
    Serial.println("Camera setup failed. Halting.");
    while (true);
  }
  getFrameQuality();
  
  if (rtspServer.init()) { 
    Serial.printf("RTSP server started successfully using default values, Connect to rtsp://%s:554/\n", WiFi.localIP().toString().c_str());
  } else { 
    Serial.println("Failed to start RTSP server"); 
  }
  
  // Create tasks for sending video, and subtitles
  xTaskCreatePinnedToCore(sendVideo, "Video", 8192, NULL, 2, &videoTaskHandle, 0);
  //xTaskCreate(sendVideo, "Video", 8192, NULL, 9, &videoTaskHandle);
  
}

void loop() {
   server.handleClient();
  if (Serial.available())
  {
    lastMessage = Serial.readStringUntil('\n');
    newMessage = true;
    //Serial.println(lastMessage);
    
  }
  vTaskDelay(20 / portTICK_PERIOD_MS);
  //vTaskDelete(NULL); // free 8k ram and delete the loop
}




