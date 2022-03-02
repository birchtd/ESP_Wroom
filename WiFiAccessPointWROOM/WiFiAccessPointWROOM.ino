/*
  WiFiAccessPointWROOM.ino provides wifi connectivity for Moana logger
  
  Adapted from example code by Elochukwu Ifediora (fedy0)
*/

#include <WiFi.h>
#include <WiFiAP.h>
#include <HTTPClient.h>
#include "MySerial.h"
#include <StreamString.h>
#include <SPIFFS.h>
#include "MD5.h"

const int version = 109;
// 100 Initial release
// 101 Changed startup messages for improved reliability
// 102 runs at 19200 baud all the time
// 103 tidied up startup messages
// 104 divides large files into <10k chunks
// 105 deal with escaped characters in SSID and password
// 106 tidied up some debug, 'success' banner is now bigger
// 107 added MD5 check
// 108 changed default ssid, added MAC to startup messages
// 109 support empty WiFi password

byte mac[6];

#define LED_BUILTIN 2   // Set the GPIO pin where you connected your test LED or comment this line out if your dev board has a built-in LED

// Credentials to be used when the ESP32 is being a server
const char *ssid = "MoanaLogger";
const char *password = "Moana";

// this is the network we'll connect to as a client
const char *defaultClientSsid = "Moana";
const char *defaultClientPassword = "Deepwatertemp$";

char suppliedClientSsid[100] = "";
char suppliedClientPassword[100] = "";

char *clientSsid = NULL;
char *clientPassword = NULL;

// Initialize the client library
WiFiClient client;

bool serverComplete = false;

uint8_t *MBbuffer = NULL;
int payloadSize = 0; // the size of the entity in MBbuffer

BufferedStream *pStream = NULL;

typedef enum
{
  FUNC_READY,
  FUNC_SERVER,
  FUNC_CLIENT
} FUNCTION;

FUNCTION fn = FUNC_READY; // what the hell are we supposed to be doing?

WiFiServer server(80);

File dataFile; // reads and writes the data file

void setup()
{
  // test code begins

  unsigned char* hash=MD5::make_hash("hello world");
  //generate the digest (hex encoding) of our hash
  char *md5str = MD5::make_digest(hash, 16);
  //print it on our serial monitor
  Serial.println(md5str);
  //Give the Memory back to the System if you run the md5 Hash generation in a loop
  free(md5str);
  //free dynamically allocated 16 byte hash from make_hash()
  free(hash);

  // test code ends
  // For compatibility with the 4G modem we start up at 115200 baud and output 'OK'
  // then switch to 19200 baud for the rest of the session
  Serial.begin(19200);
  Serial.setTimeout(1000000); // that should do, want to wait indefinitely

  delay(250); // wait a moment before sending the startup announcement

  Serial.println();
  Serial.print("[ver] ");
  Serial.println(version);

  WiFi.macAddress(mac);
  Serial.print("[mac] ");
  Serial.print(mac[0], HEX);
  Serial.print(":");
  Serial.print(mac[1], HEX);
  Serial.print(":");
  Serial.print(mac[2], HEX);
  Serial.print(":");
  Serial.print(mac[3], HEX);
  Serial.print(":");
  Serial.print(mac[4], HEX);
  Serial.print(":");
  Serial.println(mac[5], HEX);
  
  //Serial.println("[ESP32]");

  //delay(1000);
  //Serial.println("[READY]"); // not until we really are

  clientSsid = (char *)defaultClientSsid;
  clientPassword = (char *)defaultClientPassword;
  // use the defaults if there's nothing else

  setupReady();

  pStream = new BufferedStream(1024);

  //Serial.println("SPIFFS startup");
  if (SPIFFS.begin(true))
  {
    //Serial.println("SPIFFS startup complete");

    File f2 = SPIFFS.open("/ssid", "r");
    if (f2)
    {
      //Serial.println("Reading credentials");
      uint8_t c;
      String credentials = f2.readString();
      //Serial.println("Credentials:");
      //Serial.println(credentials);
      f2.close();
      if (credentials.length() > 0)
      {
        // split that into two lines
        char ssidFromFile[100];
        credentials.toCharArray(ssidFromFile, 100);
        char *pSSID = strtok(ssidFromFile, "\r");
        char *pPassword = strtok(NULL, "\r");
        pPassword++; // skip the newline
  
        strcpy(suppliedClientSsid, pSSID);
        strcpy(suppliedClientPassword, pPassword);
        clientSsid = suppliedClientSsid;
        clientPassword = suppliedClientPassword;
        //Serial.print("SSID:");
        //Serial.println(clientSsid);
        //Serial.print("PASS:");
        //Serial.println(clientPassword);
      }
      else
      {
        Serial.println("No credentials, using defaults");
      }
    }
    else
    {
      Serial.println("No credentials file");
    }
  }
  else
  {
    Serial.println("SPIFFS Mount Failed");
  }
  Serial.println("[ESP32]");
  Serial.println("");
}

#define SLEEP_DURATION 1000000
void lightSleep()
{
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION);
    esp_light_sleep_start();
}

void setupReady()
{
  fn = FUNC_READY;
  Serial.println("ESP32 in Ready mode");
}

void setupServer()
{
  fn = FUNC_SERVER;
  Serial.println("[inf] Configuring access point...");
  Serial.print("Ssid: ");
  Serial.println(ssid);

  // You can remove the password parameter if you want the AP to be open.
  //WiFi.softAP(ssid, password);
  WiFi.softAP(ssid);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("[inf] AP IP address: ");
  Serial.println(myIP);
  server.begin();

  Serial.println("[inf] Server Started");
}

void setupClient()
{
  // spiffs startup moved from here

  fn = FUNC_CLIENT;

  if (MBbuffer == NULL)
  {
    MBbuffer = (uint8_t *)malloc(100*1024);
    if (MBbuffer == NULL)
    {
      Serial.println("[PANIC] Malloc failed");
    }
    else
    {
      Serial.println("[inf] Malloc ok");
    }
  }
  else
  {
    Serial.println("[inf] MBbuffer allocated already");
  }
  
  Serial.println("[inf] Configuring client");
  WiFi.mode(WIFI_STA);
  Serial.print("[inf] ssid ");
  Serial.println(clientSsid);
  Serial.print("[inf] pswd ");
  Serial.println(clientPassword);
  WiFi.begin(clientSsid, clientPassword);

  int iter = 0;
  while ( WiFi.status() != WL_CONNECTED)
  {
    if ( WiFi.status() == WL_CONNECTED) break;

    if (++iter == 1000000)
    {
      iter = 0;
      Serial.print("[inf] still trying, status ");
      Serial.println(WiFi.status());
    }
  }
  //else
  {
    Serial.println("[CONNECT] Connected to wifi");
  }
}

void loop()
{

  // The ESP32 can be in three states:
  // ready (waiting for instructions from the CPU)
  // server - being a web server to allow SSID configuration
  // client - being a WiFi client 
  switch (fn)
  {
    case FUNC_READY:
      readyLoop();
    break;

    case FUNC_SERVER:
      serverLoop();
    break;

    case FUNC_CLIENT:
      clientLoop();
    break;
  }
}

void readyLoop()
{
  // wait for instructions from CPU, go into either client or server mode
  String cmd = Serial.readStringUntil('\n');
  if (cmd.startsWith("server"))
  {
    Serial.println("[inf] Server mode");
    setupServer();
  }
  else if (cmd.startsWith("client"))
  {
    Serial.println("[inf] Client mode");
    setupClient();
  }
  else
  {
    Serial.println("[ESP32] Ready");
  }
}

void clientLoop()
{
  // connect to WiFi network, wait for instructions from CPU
  String cmd = Serial.readStringUntil('\n');
  //Serial.println("[inf] got command:" + cmd);
  if (cmd == "quit")
  {
    Serial.println("[inf] got quit instruction");
    //WiFi.stop();
    Serial.println("[DONE]");
    setupReady();
  }
  if (cmd.startsWith("client"))
  {
    Serial.println("[CONNECT]"); // confirm that we're connected
  }
  if (cmd == "get")
  {
    Serial.println("[inf] GET");
    getExample();
  }
  if (cmd == "bget")
  {
    Serial.println("[inf] BINARY GET");
    bgetExample();
  }
  if (cmd.startsWith("get ")) // text get
  {
    doGet(cmd.substring(4));
  }
  if (cmd.startsWith("bget ")) // binary get
  {
    doGetBinary(cmd.substring(5));
  }
  if (cmd == "put")
  {
    Serial.println("[inf] PUT example");
    doPut(cmd.substring(4));
  }
  if (cmd.startsWith("put ")) // text put
  {
    Serial.println("[inf] PUT");
    doPut(cmd.substring(4));
  }

  if (cmd.startsWith("clear")) // clear the data file and get ready to write
  {
      if (dataFile) dataFile.close();
      dataFile = SPIFFS.open("/file", "w");
      if (dataFile)
        Serial.println("opened for write");
      else
        Serial.println("fail");
      Serial.println("[OK]");
  }
  if (cmd.startsWith("append ")) // append bytes to data file
  {
    String arg = cmd.substring(7);
    int numBytes = arg.toInt();
    Serial.print(">");
    while (numBytes--)
    {
      int i;
      uint8_t c;

      while (!Serial.available());
      
      i = Serial.read();
      c = i;
      dataFile.write(c);
    }
    Serial.println("[OK]");
  }

  if (cmd.startsWith("finish")) // close data file
  {
    if (dataFile) dataFile.close();
    Serial.println("[OK]");
  }

  if (cmd.startsWith("rewind")) // open data file for reading
  {
    if (dataFile) dataFile.close();
    dataFile = SPIFFS.open("/file", "r");
    if (dataFile)
    {
      Serial.print("[SIZE]");
      Serial.println(dataFile.size());
    }
    else
      Serial.println("[SIZE]0");
  }

  if (cmd.startsWith("read ")) // read bytes from data file
  {
    String arg = cmd.substring(5);
    int numBytes = arg.toInt();
    while (numBytes--)
    {
      int i;
      uint8_t c;
      i = dataFile.read();
      c = i;
      //Serial.print(i);
      //Serial.print(":[");
      Serial.write(c);
      //Serial.println("]");
    }
    // we don't have an [OK] at the end as the recipient is likely to be in cbreak mode
    // so just send the requested characters and then await the next command
  }
}

void serverLoop()
{
  WiFi.mode(WIFI_AP);
  WiFiClient client = server.available();   // listen for incoming clients

  if (client)
  {                             // if you get a client,
    Serial.println("[inf] New Client.");           // print a message out the serial port
    bool sendForm = true;
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) 
    {            // loop while the client's connected
      if (client.available()) 
      {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        // uncomment the 3 lines below if you want to see all the incoming data
        //if (c == '\r') Serial.write("\\r");
        //if (c == '\n') Serial.write("\\n");
        //Serial.write(c);                    // print it out the serial monitor

        if (c == '\n') 
        {                    // if the byte is a newline character

          // 105 Unescape the entire line - look for any % character
          //Serial.println("Processing line:");
          //Serial.println(currentLine);

          int pos = 0;
          while (pos < currentLine.length())
          {
            if (currentLine.c_str()[pos] == '%')
            {
              //Serial.print("Found an escape sequence at ");
              //Serial.println(pos);
              //Serial.println(currentLine);
              char hexString[3];
              hexString[0] = currentLine.c_str()[pos+1];
              hexString[1] = currentLine.c_str()[pos+2];
              hexString[2] = 0;
              //Serial.print("Hex value ");
              //Serial.println(hexString);
              char c = strtoul(hexString, NULL, 16);
              //Serial.print("ASCII value ");
              //Serial.println((int)c);
              if ((c == 0x91) || (c == 0x92)) c = 0x47; // single quote
              if ((c == 0x93) || (c == 0x94)) c = 0x22; // double quote
              String start = currentLine.substring(0, pos);
              String end = currentLine.substring(pos+3);
              currentLine = start + c + end;
              //Serial.print("Escaped string: ");
              //Serial.println(currentLine);
            }
            pos++;
          }

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          //Serial.println("Escaped line:");
          //Serial.println(currentLine);

          int settingIdx = currentLine.indexOf("GET /ssid.html?");
          if (settingIdx >= 0)
          {
            Serial.println("[inf] It's a submission:");
            sendForm = false; // when we send a response, it should be to say that the settings have been received/
            // we don't want to send the form again
            
            // extract ssid and pwd from query
            int queryPos = currentLine.indexOf("?");
            if (queryPos >= 0)
            {
              currentLine = currentLine.substring(queryPos);
            }
            int ampersandPos = currentLine.indexOf("&");
            if (ampersandPos > 0)
            {
              String ssid = currentLine.substring(6, ampersandPos); // should be of the form "ssid=mySsid&pwd=myPwd"

              Serial.println("[SSID] " + ssid);
              currentLine = currentLine.substring(ampersandPos+5); // should be of the form "ssid=mySsid&pwd=myPwd
              int spacePos = currentLine.indexOf(" HTTP/1.1");
              String pwd = currentLine.substring(0,spacePos); // should be of the form "ssid=mySsid&pwd=myPwd

              Serial.println("[PWD] " + pwd);

              File f = SPIFFS.open("/ssid", "w");
              if (f)
              {
                f.println(ssid);
                f.println(pwd);
                f.close();
                Serial.println("Created credentials file");
              }
              else
              {
                Serial.println("Can't open credentials file to write");
              }

            }
          }
          else if (currentLine.length() == 0) 
          {
            // a blank line indicates that the request is complete, so send something back
            // this could be either the form or an acknowledgement of the new settings
            sendSomething(client, sendForm);

            // break out of the while loop: - essentially drop the connection at this point
            Serial.println("[inf] page delivered");
            break;
            
          }
          else
          {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        }
        else if (c != '\r')
        {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // close the connection:
    client.stop();
    Serial.println("[inf] Client Disconnected.");

    if (serverComplete)
    {
      Serial.println("[DONE] Captured credentials.");
      serverComplete = false;
      WiFi.softAPdisconnect (true);
      // might as well return to command mode now
      setupReady();
    }
  }
}

void sendSomething(WiFiClient client, bool sendForm)
{
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();

  if (sendForm)
  {
    Serial.println("[inf] Sending form.");
    client.println("<!DOCTYPE html>");
    client.println("<script>");
    client.println("  function submitForm()");
    client.println("{");
    client.println("    var urlParam = \"SSID=\"+document.getElementById(\"ssid\").value + \"&PWD=\"+document.getElementById(\"password\").value;");
    client.println("    var URL = \"http://192.168.4.1/ssid.html\";");
    client.println("    var encordedUrl = URL+\"?\"+encodeURI( urlParam );");
    client.println("    document.location.href = encordedUrl;");
    client.println("}");
    client.println("</script>");
    
    client.println("<body>");
    client.println("Zebra-Tech Moana Logger configuration<br>");
    client.println("Please enter the SSID and password for your WiFi network<br>");
    client.println("        <div class=\"well well-sm\">");
    client.println("          <form class=\"form-horizontal\" role=\"form\" action=\"http://192.168.4.1/init.html\">");
    client.println("              <fieldset>");
    client.println("                    <!-- Text input-->");
    client.println("                    <div class=\"form-group\">");
    client.println("                        <label class=\"col-sm-2 control-label\" for=\"ssid\">SSID</label>");
    client.println("                        <div class=\"col-sm-10\">");
    client.println("                            <input id=\"ssid\" name=\"init_user\" type=\"text\" placeholder=\"SSID\" class=\"form-control\" required=\"required\">");
    client.println("                        </div>");
    client.println("                        <label class=\"col-sm-2 control-label\" for=\"password\">Password</label>");
    client.println("                        <div class=\"col-sm-10\">");
    client.println("                            <input id=\"password\" name=\"init_pwd\" type=\"text\" placeholder=\"Password\" class=\"form-control\" required=\"required\">");
    client.println("                        </div>");
    client.println("                    </div>");
    client.println("                    <!-- Button -->");
    client.println("                    <div class=\"form-group\">");
    client.println("                        <label class=\"col-sm-2 control-label\" for=\"button\"></label>");
    client.println("                        <div class=\"col-sm-offset-2 col-sm-10\">");
    client.println("                            <button id=\"button\" class=\"btn btn-lg btn-primary\" onclick=\"submitForm()\" type=\"button\">Submit</button>");
    client.println("                        </div>");
    client.println("                    </div>");
    client.println("              </fieldset>");
    client.println("            </form>");
    client.println("        </div>");
    client.println("</body>");
    client.println();
  }
  else
  {
    Serial.println("[GOT_CRED]");
    client.println("<H1>Settings updated</H1>");
    client.println();
    serverComplete = true;
  }
}

void getExample()
{
    HTTPClient gxhttp;
    gxhttp.begin("https://dataupload.zebra-tech.co.nz/FIRMWARE/version/0000.txt");
    int r = gxhttp.GET();
    if (r == 200)
    {
      Serial.print("[PAYLOAD] ");
      String payload = gxhttp.getString();
      Serial.println(payload.length());
      Serial.println(payload);
      Serial.println("[DONE]");
    }
    else
    {
      Serial.print("[FAIL] ");
      Serial.println(r);
    }
    gxhttp.end();
}

void bgetExample()
{
    fHTTPClient bghttp;
    bghttp.begin("https://dataupload.zebra-tech.co.nz/FIRMWARE/package/moana241.zm");
    int r = bghttp.GET();
    if (r == 200)
    {
      Serial.print("[PAYLOAD] ");
      Serial.print("Expecting ");
      Serial.print(bghttp.getSize());
      Serial.println(" bytes");

      File f = SPIFFS.open("/file", "w");
      if (f)
      {
        Serial.println("Created file");
        bghttp.writeToStream(&f);
        f.close();
        Serial.println("Wrote file");

        File r = SPIFFS.open("/file", "r");
        Serial.print("Readback size is ");
        Serial.println(r.size());
        r.close();
      }
      else
        Serial.println("Can't open file to write");
      
      Serial.println("[DONE]");
    }
    else
    {
      Serial.print("[FAIL] ");
      Serial.println(r);
    }
    bghttp.end();
}

void doPutExample(String cmd)
{
  String payload = "Here is a test payload";
  uint8_t* pPayload = (uint8_t*)payload.c_str();
    HTTPClient pxhttp;
    pxhttp.begin("https://dataupload.zebra-tech.co.nz/Development/saveFile.php?sn=1003&key=300&fileName=frog.txt&start=1&end=1");
    int r = pxhttp.PUT(pPayload, payload.length());
    if (r == 200)
    {
      Serial.print("[PAYLOAD] ");
      String entity = pxhttp.getString();
      Serial.println(entity.length());
      Serial.println(entity);
      Serial.println("[DONE]");
    }
    else
    {
      Serial.print("[FAIL] ");
      Serial.println(r);
    }
    pxhttp.end();
}

HTTPClient dphttp;

void doPut(String url)
{
    int bytesSentSoFar;
    int bytesLeftToSend;
    const int maxChunk = 8192;
    int bytesThisChunk;
    bool success = true;

    // can't do dphttp.begin here as the url will be different for each chunk.
    //dphttp.begin(url);

    // We send whatever is stored in the file
    // Currently that's limited to the 100k RAM buffer, but the logger can fragment
    // into multiple PUT operations if needed
    if (dataFile) dataFile.close();
    dataFile = SPIFFS.open("/file", "r");
    payloadSize = dataFile.size();
    int destIdx = 0;

    Serial.print("[inf] total payload ");
    Serial.println(payloadSize);

    bytesLeftToSend = payloadSize;
    bytesSentSoFar = 0;

    // Initialise the MD5 workspace
    MD5_CTX context;
    unsigned char * hash = (unsigned char *) malloc(16);
    MD5::MD5Init(&context);
    char *md5str = NULL;

    do
    {
      // determine how much we're gong to send this time
      if (bytesLeftToSend < maxChunk) bytesThisChunk = bytesLeftToSend;
      else bytesThisChunk = maxChunk;
  
      bytesLeftToSend -= bytesThisChunk; // will be zero for the last chunk that we send
  
      // read the chunk from the file
      for (int i = 0; i < bytesThisChunk; i++)
      {
        int j;
        uint8_t c;
        j = dataFile.read();
        c = j;
        MBbuffer[i] = c;
      }

      MD5::MD5Update(&context, MBbuffer, bytesThisChunk);

      if (bytesLeftToSend == 0)
      {
        MD5::MD5Final(hash, &context);
        md5str = MD5::make_digest(hash, 16);
      }

      Serial.print("[inf] sending ");
      Serial.println(bytesThisChunk);
      Serial.print("[inf] remaining ");
      Serial.println(bytesLeftToSend);

      // send the chunk to the server
      success &= doPutChunk(url, bytesThisChunk, bytesSentSoFar == 0, bytesLeftToSend == 0, md5str);
      bytesSentSoFar += bytesThisChunk;
    } while (bytesLeftToSend > 0);

    free(md5str);
    free(hash);

    dataFile.close();

    //dphttp.end();

    if (success)
    {
      Serial.println("[SUCCESS] ");
      Serial.println("[DONE]");
    }
    else
    {
      Serial.println("[FAIL]");
    }
}

bool doPutChunk(String url, int size, bool start, bool end, char *pMD5)
{
  String massagedUrl = url;
  if (start) massagedUrl += "&start=1";
  if (end)
  {
    massagedUrl += "&end=1";
    massagedUrl += "&sum=";
    massagedUrl += pMD5;
    Serial.print("[info] MD5 is ");
    Serial.println(pMD5);
  }
  
    Serial.print("[info] Sending ");
    Serial.print(size);
    Serial.println(" bytes to");
    Serial.println(massagedUrl.substring(0,60));
    Serial.println(massagedUrl.substring(60));
    //Serial.println(massagedUrl);

    dphttp.begin(massagedUrl);

    int r = dphttp.PUT(MBbuffer, size);
    if (r == 200)
    {
      Serial.print("[good] ");
      String entity = dphttp.getString();
      Serial.println(entity.length());
      Serial.println(entity);
      Serial.println("[/good]");
      dphttp.end();
      return true;
    }
    else
    {
      Serial.println("[fail] ");
      String entity = dphttp.getString();
      Serial.println(entity.length());
      Serial.println(entity);
      Serial.println(r);
      Serial.println("[/fail]");
      dphttp.end();
      return false;
    }
}

void doGet(String url)
{
    fHTTPClient dghttp;
    dghttp.begin(url);
    int r = dghttp.GET();
    if (r == 200)
    {
      Serial.print("[PAYLOAD] ");
      String payload = dghttp.getString();
      Serial.println(payload.length());
      Serial.println(payload);
      Serial.println("[DONE]");
    }
    else
    {
      Serial.print("[FAIL] ");
      Serial.println(r);
    }
    dghttp.end();
}

void doGetBinary(String url)
{
    WiFiClient client;
    HTTPClient dgbhttp;
    dgbhttp.begin(url);
    int r = dgbhttp.GET();
    if (r == 200)
    {
      WiFiClient *pClient = dgbhttp.getStreamPtr();

      Serial.print("pClient is ");
      //Serial.print(pClient);
      Serial.println((unsigned int)pClient,HEX);

      Serial.print("Stream has ");
      Serial.print(pClient->available());
      Serial.println(" bytes available");

      Serial.print("[PAYLOAD] ");
      int payloadSize = dgbhttp.getSize();
      Serial.println(payloadSize);

      if (dataFile) dataFile.close();
      dataFile = SPIFFS.open("/file", "w");
      if (dataFile)
      {
        Serial.println("opened for write");
        dgbhttp.writeToStream(&dataFile);
      }
      else
      {
        Serial.println("can't open local file");
      }
      Serial.println("[OK]");
    }
    else
    {
      Serial.print("[FAIL] ");
      Serial.println(r);
    }
    dgbhttp.end();
}

void getStuff()
{
    Serial.println("[inf] Starting connection...");

    IPAddress result;
    int err = WiFi.hostByName("dataupload.zebra-tech.co.nz", result);
    if(err == 1)
    {
      Serial.print("[inf] IP address: ");
      Serial.println(result);
    }
    else
    {
      Serial.print("[FAIL] Error code: ");
      Serial.println(err);
    }
    
    // if you get a connection, report back via serial:

    if (client.connect(result, 80))
    {
      Serial.println("[SUCCESS] connected");
      // Make a HTTP request:
      client.println("GET /FIRMWARE/version/0000.txt HTTP/1.0");
      //client.println("Host:dataupload.zebra-tech.co.nz/FIRMWARE/version/0000.txt HTTP/1.0");
      //client.println("Connection:close");
      client.println();

      ////////
      unsigned long timeout = millis();
      while (client.available() == 0) 
      {
        if (millis() - timeout > 5000) 
        {
          Serial.println(">>> Client Timeout !");
          client.stop();
          return;
        }
      }
    
      // Read all the lines of the reply from server and print them to Serial
      while (client.available()) 
      {
        String line = client.readStringUntil('\r');
        Serial.print(line);
      }
    
      Serial.println();
      Serial.println("closing connection");
      client.stop();
      ////////
    }
    else
    {
      Serial.println("[FAIL] Failed to connect");
    }
}
