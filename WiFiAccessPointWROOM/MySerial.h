class fHTTPClient : public HTTPClient
{
  public:
  int getOne(void) { return 1; }
  WiFiClient* get_client(void) { return _client; }
};

class fifo
{
  public:
    fifo(int s)
    {
      pBuffer = (uint8_t *)malloc(s);
      pushIdx = 0;
      popIdx = 0;
      level = 0;
      bufSize = s;
    }

    void push(uint8_t c)
    {
      if (level < bufSize)
      {
        pBuffer[pushIdx] = c;
        level++;
        if (++pushIdx == bufSize) pushIdx = 0;
      }
      else
        level++; // in the event of an overrun just count the bytes
    }

    uint8_t pop(void)
    {
      uint8_t c = 0;
      if (level > 0)
      {
        c = pBuffer[popIdx];
        level--;
        if (++popIdx == bufSize) popIdx = 0;
      }

      return c;
    }

    int getLevel(void)
    {
      return level;
    }

  private: 
    uint8_t *pBuffer;
    int bufSize;
    int pushIdx;
    int popIdx;
    int level;
};

class BufferedStream : public Stream {
  public:
    BufferedStream(int s)
    {
      pQueue = new fifo(s);
    }

    size_t write(const uint8_t data)
    {
      //Serial.print("[PUSH] ");
      //Serial.println((char)data);
      pQueue->push(data);
      return 1;
    }
    using Print::write; // pull in write(str) and write(buf, size) from Print

    void poll(void) {
      if (pQueue->getLevel())
      {
        Serial.write(pQueue->pop());
      }
    }
    void begin(unsigned long speed) {
      Serial.begin(speed);
    }
    void end() {
      Serial.end();
    }
    int available(void)
    {
      int level = pQueue->getLevel();
      //Serial.print("[AVAILABLE] ");
      //Serial.println(level);
      return level;
    }
    int peek(void) 
    {
      return Serial.peek();
    }
    int read(void)
    {
      uint8_t c = pQueue->pop();
      //Serial.print("[POP] ");
      //Serial.println((char)c);
      return c;
    }
    void flush(void) {
      Serial.print("[FLUSH]");
    }
  private:
    fifo *pQueue = NULL;
};
