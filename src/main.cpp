#include <Arduino.h>
#include <algorithm>
extern const uint8_t loftJpgStart[] asm("_binary_assets_loft_compact_jpg_start");
extern const uint8_t loftJpgEnd[] asm("_binary_assets_loft_compact_jpg_end");
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include "arcade.hpp"
#include "kanji.hpp"
#include "arcade_rfid.hpp"
#include "booth.hpp"
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace {
constexpr size_t rate = 16000, block = 640, capacity = rate * 24;
constexpr uint32_t sky = 0x242343, lilac = 0xdad4fb, cream = 0xfff2d4;
constexpr uint32_t pink = 0xffafc6, gold = 0xffd780, ink = 0x292447;
enum class Mode { Choose, Work, JapaneseMenu, Japanese, Kanji, Quote, Booth, Ready, Recording, Review, Playing, Fault };
Mode mode = Mode::Choose;
int16_t* samples;
size_t count = 0;
bool reel = true, saved = false, inInbox = false;
char clipId[17] = {};
uint32_t checksum, lastOffer, lastPaint;
String command;
char weather[40]="NYC: sync from Mac";
char tasks[3][43]={"Film Luna's first voice demo","Review the slow morning Reel","Practise Genki Lesson 5"};
bool done[3]={false,false,false};
// Inspirational quotes: built-in until Tanya loads her own from the Mac (kept across restarts).
constexpr int maxQuotes=24, quoteSize=141;
char quotes[maxQuotes][quoteSize]={
  "You are allowed to be a masterpiece and a work in progress.",
  "Small, kind steps still cross the whole sky.",
  "Be the calm one in the room. Then be the brave one.",
  "Read the clause twice. Trust yourself once.",
  "Rest is part of the work, not a reward for it.",
  "Speak slowly. Even the moon takes a month.",
  "Make the thing only you would make."};
int quoteCount=7, quoteAt=-1, incoming=-1;
Preferences store;
// Pocket Luna on the StickS3 eats what Tanya earns here: kanji moons, ticked tasks, quiz stamps.
uint32_t petFood=0, lastFeedCast=0;
int kanjiMoonsSeen=0, quizScoreSeen=0;
uint16_t petEvent=0; uint8_t petKind=0;
int kanjiTriesSeen=0; bool quizWasFeedback=false;
// Tell Pocket Luna what just happened; `amount` moons of food come with it (0 for none).
void earn(uint32_t amount, uint8_t kind) {
  ++petEvent; petKind=kind;
  if (amount) {
    petFood+=amount;
    Preferences pet; if (pet.begin("lunapet", false)) { pet.putUInt("total", petFood); pet.end(); }
  }
  lastFeedCast=0;   // tell her straight away
}
void loadQuotes() {
  if (!store.begin("luna", true)) return;
  const int n=store.getInt("count", 0);
  if (n>0 && n<=maxQuotes) {
    quoteCount=0;
    for (int i=0;i<n;i++) {
      char key[12]; snprintf(key,sizeof(key),"q%d",i);
      if (store.getString(key, quotes[quoteCount], quoteSize)>0) quoteCount++;
    }
    if (!quoteCount) { quoteCount=1; snprintf(quotes[0],quoteSize,"Load your quotes from the Mac."); }
  }
  store.end();
}
void saveQuotes() {
  if (!store.begin("luna", false)) return;
  store.clear(); store.putInt("count", quoteCount);
  for (int i=0;i<quoteCount;i++) { char key[12]; snprintf(key,sizeof(key),"q%d",i); store.putString(key, quotes[i]); }
  store.end();
}

void label(const char* text, int x, int y, int size, uint32_t color) {
  auto& s = M5.Display;
  s.setTextColor(color); s.setTextSize(size); s.setCursor(x, y); s.print(text);
}
// Draw one of Tanya's pixel-art images; transparent pixels leave the screen alone.
void img(const uint16_t* data, int w, int h, int x, int y, bool keyed = true) {
  M5.Display.setSwapBytes(true);
  if (keyed) M5.Display.pushImage(x, y, w, h, data, art::clear); else M5.Display.pushImage(x, y, w, h, data);
  M5.Display.setSwapBytes(false);
}
void plaqueText(const char* text, int cx, int cy, int room) {
  auto& s = M5.Display;
  s.setFont(&fonts::DejaVu18); s.setTextSize(1);
  if (s.textWidth(text) > room) s.setFont(&fonts::DejaVu12);
  s.setTextColor(cream); s.setTextDatum(textdatum_t::middle_center);
  s.drawString(text, cx, cy);
  s.setTextDatum(textdatum_t::top_left); s.setFont(&fonts::Font0);
}
// The gold-edged plaque button. Art exists for these widths; anything else falls back to a plain button.
void button(int x, int width, const char* text, uint32_t color) {
  const uint16_t* plaque = width==112 ? art::button112 : width==146 ? art::button146 : width==180 ? art::button180 : width==300 ? art::button300 : nullptr;
  if (!plaque) { M5.Display.fillRoundRect(x, 182, width, 44, 9, color); label(text, x + 10, 197, 2, ink); return; }
  img(plaque, width, art::button112H, x, 181);
  plaqueText(text, x + width/2, 181 + art::button112H/2 + 1, width - 30);
}
// Home tiles: Tanya's plaque art, with the label on the plain right-hand side.
const uint16_t* const tileUp[4] = {art::tileWork, art::tileJapanese, art::tileQuotes, art::tileBooth};
const uint16_t* const tileDown[4] = {art::tileWorkDown, art::tileJapaneseDown, art::tileQuotesDown, art::tileBoothDown};
const char* const tileName[4] = {"Work", "Japanese", "Quotes", "Photos"};
void tile(int index, bool pressed = false) {
  const int x = 4 + (index%2)*158, y = 122 + (index/2)*59;
  M5.Display.fillRect(x, y, art::tileWorkW, art::tileWorkH, sky);
  img(pressed ? tileDown[index] : tileUp[index], art::tileWorkW, art::tileWorkH, x, y);
  plaqueText(tileName[index], x + 102, y + 28, 92);
}
// Word-wrapped, centred text for the quote card.
void wrapped(const char* text, int x, int y, int width, int height) {
  auto& s = M5.Display;
  const lgfx::IFont* sizes[] = {&fonts::DejaVu24, &fonts::DejaVu18, &fonts::DejaVu12};
  for (const auto* font : sizes) {
    s.setFont(font);
    const int lineHeight = s.fontHeight() + 4;
    char lines[8][64]; int n = 0; char line[64] = ""; bool fits = true;
    char copy[quoteSize]; snprintf(copy, sizeof(copy), "%s", text);
    for (char* word = strtok(copy, " "); word; word = strtok(nullptr, " ")) {
      char trial[130]; snprintf(trial, sizeof(trial), line[0] ? "%s %s" : "%s%s", line, word);
      if (s.textWidth(trial) <= width && strlen(trial) < sizeof(line)) { snprintf(line, sizeof(line), "%s", trial); continue; }
      if (n == 7 || !line[0]) { fits = false; break; }
      snprintf(lines[n++], 64, "%s", line); snprintf(line, sizeof(line), "%.63s", word);
    }
    if (fits && line[0] && n < 8) snprintf(lines[n++], 64, "%s", line);
    if (!fits || n * lineHeight > height) { if (font != sizes[2]) continue; }
    s.setTextColor(ink); s.setTextDatum(textdatum_t::top_center);
    int top = y + std::max(0, (height - n * lineHeight) / 2);
    for (int i = 0; i < n; ++i) s.drawString(lines[i], x + width/2, top + i * lineHeight);
    s.setTextDatum(textdatum_t::top_left); s.setFont(&fonts::Font0);
    return;
  }
}
void moon(int x, int y, int radius, uint32_t back);
// Moon Prism sticker: a burst of rings and a rising chime, then the room changes.
void transform(const char* title) {
  auto& s = M5.Display;
  M5.Speaker.begin(); M5.Speaker.setVolume(120);
  const float notes[] = {659, 784, 988, 1319, 1568};
  const uint32_t rings[] = {pink, gold, lilac, cream};
  (void)rings;
  for (int i = 0; i < 6; ++i) {              // ribbons of light spiral out into a ring of stars
    if (i < 5) M5.Speaker.tone(notes[i], 120);
    s.fillScreen(0x1a0f24u);
    art::drawRle(art::transforms[i], 240);
    delay(95);
  }
  img(art::banner, art::bannerW, art::bannerH, 0, 70);
  art::words("Moon Prism", 160, 112, &fonts::DejaVu18, gold);
  art::words(title, 160, 134, &fonts::DejaVu12, cream);
  delay(850);
}
void paint();
void earn(uint32_t amount, uint8_t kind);
char voiceNote[40] = "";
void transform(const char* title);
// Live conversation is a separate program in the CoreS3's second slot. Restart into it;
// it hands control back to the Loft by itself when the chat ends.
void talkWithLuna() {
  const esp_partition_t* voice = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
  esp_app_desc_t about;
  if (!voice || esp_ota_get_partition_description(voice, &about) != ESP_OK) {
    snprintf(voiceNote, sizeof(voiceNote), "Voice isn't installed yet"); paint(); return;
  }
  earn(0, 5);
  transform("Talk with Luna");
  if (esp_ota_set_boot_partition(voice) != ESP_OK) { snprintf(voiceNote, sizeof(voiceNote), "Couldn't switch to voice"); paint(); return; }
  delay(200);
  esp_restart();
}
uint16_t pokeSeen = 0; bool pokeKnown = false;
uint32_t photoId = 0;
// Run the photobooth, then offer the picture to the Mac.
Mode boothFrom = Mode::Choose;        // where to go back to after a photo
void takePhoto() {
  if (mode != Mode::Booth) boothFrom = mode;
  if (boothFrom == Mode::Japanese) arcade::leave();
  mode = Mode::Booth;
  // A second double-press on the stick cancels the countdown.
  auto stickCancel = []() {
    wand::Poke poke;
    if (!radio::take(wand::kPoke, poke)) return false;
    radio::unpair();
    const bool fresh = poke.seq != pokeSeen;
    pokeSeen = poke.seq; pokeKnown = true;
    return fresh;
  };
  const bool took = booth::session(stickCancel);
  if (!took && booth::cancelled) {            // back to where she was, no photo
    Serial.println("LUNA/1 SNAP CANCELLED");
    mode = boothFrom;
    if (mode==Mode::Japanese) arcade::paint(); else if (mode==Mode::Kanji) kanji::paint(); else paint();
    return;
  }
  if (took) { photoId = esp_random(); earn(1, 4); Serial.printf("LUNA/1 SNAPPED %08lx\n", (unsigned long)photoId); }
  paint();
}
void sendPhoto() {
  if (!booth::hasPhoto) return;
  Serial.printf("LUNA/1 PHOTO %08lx %d %d\n", (unsigned long)photoId, booth::width, booth::height);
  size_t sent = 0; uint32_t progress = millis();
  while (sent < booth::bytes && millis() - progress < 3000) {
    int room = Serial.availableForWrite();
    if (room <= 0) { delay(1); continue; }
    size_t n = Serial.write(booth::photo + sent, std::min<size_t>(room, booth::bytes - sent));
    if (n) { sent += n; progress = millis(); }
  }
  Serial.println();
}
void nextQuote() {
  if (quoteCount < 2) { quoteAt = 0; return; }
  if (quoteAt < 0) quoteAt = esp_random() % quoteCount;       // a different starting point each power-on
  else quoteAt = (quoteAt + 1) % quoteCount;                  // then every press shows a different one
}
void moon(int x, int y, int radius, uint32_t back) {
  M5.Display.fillCircle(x, y, radius, gold);
  M5.Display.fillCircle(x+radius/3, y-radius/3, radius*4/5, back);
}
void paint() {
  auto& s=M5.Display;
  if (mode == Mode::Japanese) { arcade::paint(); return; }
  if (mode == Mode::Kanji) { kanji::paint(); return; }
  // Screens with their own full painting skip the plain header.
  if (mode == Mode::Work || mode == Mode::Quote || mode == Mode::JapaneseMenu) {
    img(mode == Mode::Work ? art::bgWork : mode == Mode::Quote ? art::bgQuote : art::bgJapanese, 320, 240, 0, 0, false);
  } else if (mode != Mode::Booth) {
    s.fillScreen(sky);
  }
  if (mode == Mode::Work) {
    for (int i=0;i<3;i++) {
      int y=76+i*31;
      s.fillRoundRect(10,y,300,28,6,done[i]?lilac:cream);
      img(done[i] ? art::checkDone : art::checkEmpty, art::checkDoneW, art::checkDoneH, 14, y+2);
      s.setFont(&fonts::DejaVu12);
      label(tasks[i],44,y+8,1,ink);
      s.setFont(&fonts::Font0);
    }
    s.setFont(&fonts::DejaVu12);
    label((done[0]||done[1]||done[2])?"One more star for your sky, Tanya.":"One thing at a time, Tanya.",14,170,1,gold);
    s.setFont(&fonts::Font0);
    button(10, 112, "Home", lilac);
    return;
  }
  if (mode == Mode::JapaneseMenu) {
    img(art::button300, 300, art::button112H, 10, 74);                 // talk: the full-width plaque
    img(art::iconSpeaker, 24, 24, 30, 85);
    art::words("Talk with Luna", 168, 98, &fonts::DejaVu18, cream);
    img(art::button148, 148, art::button112H, 10, 126); img(art::button148, 148, art::button112H, 162, 126);
    art::words("Quiz", 84, 150, &fonts::DejaVu18, cream);
    art::words("Kanji", 236, 150, &fonts::DejaVu18, cream);
    if (voiceNote[0]) { s.fillRoundRect(130, 186, 182, 36, 8, 0x2a1a33u); art::words(voiceNote, 221, 204, &fonts::DejaVu9, pink); }
    button(10, 112, "Home", cream);
    return;
  }
  if (mode == Mode::Quote) {
    img(art::quoteCard, art::quoteCardW, art::quoteCardH, 8, 34, false);
    wrapped(quotes[quoteAt < 0 ? 0 : quoteAt], 28, 46, 264, 116);
    button(10, 112, "Home", lilac);
    button(130, 180, "Another one", gold);
    return;
  }
  if (mode != Mode::Choose) { label("LUNA'S MOON COMPACT", 12, 12, 1, lilac); moon(294, 21, 11, sky); }
  if (mode == Mode::Choose) {
    // The window band, then four plaque tiles.
    label("LUNA'S LOFT", 12, 12, 1, lilac);
    moon(294, 21, 11, sky);
    img(art::band, art::bandW, art::bandH, 0, 29, false);
    s.setFont(&fonts::DejaVu12); label(weather, 106, 8, 1, cream); s.setFont(&fonts::Font0);
    for (int i = 0; i < 4; ++i) tile(i);
    return;
  }
  if (mode == Mode::JapaneseMenu) {
    label("JAPANESE / GENKI I LESSON 5", 14, 39, 1, cream);
    img(art::wide, art::wideW, art::wideH, 8, 56); img(art::wide, art::wideW, art::wideH, 8, 118);
    img(art::iconSparkle, 24, 24, 30, 71); img(art::iconTag, 24, 24, 30, 133);
    s.setFont(&fonts::DejaVu18); s.setTextColor(cream); s.setTextDatum(textdatum_t::middle_center);
    s.drawString("Quick quiz", 160, 83);
    s.drawString("Kanji practice", 160, 145);
    s.setTextDatum(textdatum_t::top_left); s.setFont(&fonts::Font0);
    button(10, 112, "Home", cream);
    return;
  }
  if (mode == Mode::Booth) {
    if (booth::hasPhoto) {
      booth::showPhoto();
      s.fillRoundRect(96, 180, 62, 24, 12, cream); s.fillRoundRect(164, 180, 62, 24, 12, cream);
      label("Again", 112, 188, 1, ink); label("Back", 183, 188, 1, ink);
    } else {
      label("PHOTOBOOTH", 14, 39, 2, cream);
      label(booth::problem[0] ? booth::problem : "No photo yet.", 14, 80, 1, pink);
      button(10, 112, "Home", lilac); button(130, 180, "Try again", gold);
    }
    return;
  }
  if (mode == Mode::Quote) {
    s.fillRoundRect(8, 36, 304, 140, 10, cream);
    moon(30, 54, 8, cream);
    wrapped(quotes[quoteAt < 0 ? 0 : quoteAt], 24, 46, 272, 122);
    button(10, 112, "Home", lilac);
    button(130, 180, "Another one", gold);
    return;
  }
  s.fillRoundRect(10, 40, 300, 129, 9, cream);
  s.fillTriangle(27, 78, 28, 54, 48, 72, ink);
  s.fillTriangle(51, 69, 68, 53, 70, 82, ink);
  s.fillCircle(49, 87, 23, ink);
  moon(49, 76, 6, ink);
  s.fillCircle(40, 90, 2, gold); s.fillCircle(58, 90, 2, gold);
  {
    label(reel ? "Instagram Reel" : "Substack idea", 82, 77, 2, ink);
    switch (mode) {
      case Mode::Ready:
        label("Record a voice note up to 24 seconds", 23, 137, 1, ink);
        button(10, 85, "Back", lilac); button(102, 208, "Record", pink); break;
      case Mode::Recording:
        label("Listening", 23, 123, 2, ink);
        button(10, 300, "Finish recording", pink); break;
      case Mode::Review:
        label(inInbox ? "Filed in your local demo inbox" : saved ? "In your Mac satchel" : "Clip in memory - connect your Mac", 23, 137, 1, ink);
        button(10, 146, "Replay", pink);
        if (saved) button(164, 146, "New card", lilac);
        break;
      case Mode::Playing:
        label("Playing your voice note", 23, 137, 1, ink); break;
      default:
        label("Audio unavailable - restart to retry", 16, 137, 1, ink); break;
    }
  }
}
void change(Mode next) { mode=next; paint(); }
void finish() {
  while (M5.Mic.isRecording()) delay(1);
  M5.Mic.end();
  if (!count) { change(Mode::Ready); return; }
  snprintf(clipId, sizeof(clipId), "%08lx%08lx",
           (unsigned long)esp_random(), (unsigned long)esp_random());
  uint32_t crc = 0xffffffffu;
  auto* raw = reinterpret_cast<const uint8_t*>(samples);
  for (size_t i=0; i<count*2; ++i) {
    crc ^= raw[i];
    for (int bit=0; bit<8; ++bit) crc = (crc>>1) ^ ((crc&1u) ? 0xedb88320u : 0u);
  }
  checksum=~crc; saved=false; inInbox=false; lastOffer=millis()-2500; change(Mode::Review);
}
void usbCommand() {
  const char* line=command.c_str();
  if (strncmp(line,"LUNA/1 TEMP ",12)==0) {
    if (strcmp(weather,line+12)!=0) {
      snprintf(weather,sizeof(weather),"%s",line+12);
      // Remember the last reading so it still shows after the Mac is unplugged.
      Preferences wx; if (wx.begin("lunawx", false)) { wx.putString("temp", weather); wx.end(); }
      if (mode==Mode::Choose) paint();
    }
    return;
  }
  if (strcmp(line,"LUNA/1 SNAP")==0) { takePhoto(); return; }
  if (strcmp(line,"LUNA/1 GETPHOTO")==0) { sendPhoto(); return; }
  if (strcmp(line,"LUNA/1 SCREEN")==0) {      // send what is on the display, for checking the design from the Mac
    static uint16_t row[320];
    Serial.println("LUNA/1 SCREENSHOT 320 240");
    for (int y=0; y<240; ++y) {
      M5.Display.readRect(0, y, 320, 1, row);
      size_t sent=0; uint32_t progress=millis();
      while (sent<sizeof(row) && millis()-progress<2000) {
        int room=Serial.availableForWrite();
        if (room<=0) { delay(1); continue; }
        size_t n=Serial.write(reinterpret_cast<uint8_t*>(row)+sent, std::min<size_t>(room, sizeof(row)-sent));
        if (n) { sent+=n; progress=millis(); }
      }
    }
    Serial.println(); return;
  }
  if (strncmp(line,"LUNA/1 GO ",10)==0) {     // jump to a screen, for checking the design from the Mac
    const char* where=line+10;
    if (!strcmp(where,"home")) change(Mode::Choose);
    else if (!strcmp(where,"work")) change(Mode::Work);
    else if (!strcmp(where,"quote")) { nextQuote(); change(Mode::Quote); }
    else if (!strcmp(where,"japanese")) change(Mode::JapaneseMenu);
    else if (!strcmp(where,"kanji")) { mode=Mode::Kanji; kanji::enter(); }
    else if (!strcmp(where,"quiz")) { mode=Mode::Japanese; arcade::enter(); }
    return;
  }
  if (strcmp(line,"LUNA/1 QUOTES BEGIN")==0) { incoming=0; return; }
  if (strncmp(line,"LUNA/1 QUOTE ",13)==0) {
    if (incoming>=0 && incoming<maxQuotes && line[13]) snprintf(quotes[incoming++],quoteSize,"%s",line+13);
    return;
  }
  if (strcmp(line,"LUNA/1 QUOTES END")==0) {
    if (incoming>0) { quoteCount=incoming; quoteAt=-1; saveQuotes(); Serial.printf("LUNA/1 QUOTES SAVED %d\n",quoteCount); if (mode==Mode::Quote) { nextQuote(); paint(); } }
    incoming=-1; return;
  }
  if (strncmp(line,"LUNA/1 TASK ",12)==0 && strlen(line)>=17) {
    int i=line[12]-'0';
    if (i>=0 && i<3 && line[13]==' ' && (line[14]=='0'||line[14]=='1') && line[15]==' ') {
      done[i]=line[14]=='1'; snprintf(tasks[i],sizeof(tasks[i]),"%s",line+16);
      if (mode==Mode::Work) paint();
    }
    return;
  }
  String id(clipId);
  if (id.length() && command == "LUNA/1 INBOX " + id) {
    inInbox=true; if (mode==Mode::Review) paint(); return;
  }
  if (mode != Mode::Review) return;
  if (command == "LUNA/1 ACK " + id) { saved=true; paint(); }
  if (command != "LUNA/1 GET " + id) return;
  Serial.printf("LUNA/1 AUDIO %s %s %u %08lx\n", clipId, reel?"reel":"substack",
                (unsigned)count, (unsigned long)checksum);
  const uint8_t* p=reinterpret_cast<const uint8_t*>(samples);
  size_t sent=0;
  uint32_t progress=millis();
  while (sent<count*2 && millis()-progress<3000) {
    int room=Serial.availableForWrite();
    if (room<=0) { delay(1); continue; }
    size_t n=Serial.write(p+sent, std::min<size_t>(room, count*2-sent));
    if (n) { sent+=n; progress=millis(); }
  }
  Serial.println();
}
}
void setup() {
  auto cfg=M5.config();
  M5.begin(cfg); Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // never stall touch handling when no Mac is reading USB
  if (M5.getBoard()!=m5::board_t::board_M5StackCoreS3) {
    M5.Display.print("Luna requires CoreS3 K128"); while (true) delay(100);
  }
  if (M5.Display.width()<M5.Display.height()) M5.Display.setRotation(1);
  // Voice-note recording is retired, so its large audio buffer is no longer reserved at start-up.
  samples=nullptr;
  Serial.printf("LUNA/1 BOOT psram=%u heap=%u\n",(unsigned)ESP.getPsramSize(),(unsigned)ESP.getFreeHeap());
  img(art::splash, art::splashW, art::splashH, 0, 0, false);   // the crystal compact opens
  delay(1600);
  loadQuotes();
  { Preferences pet; if (pet.begin("lunapet", true)) { petFood=pet.getUInt("total", 0); pet.end(); } }
  radio::begin(wand::kPoke);     // hears the stick's double-press; also carries Luna's food
  { Preferences wx; if (wx.begin("lunawx", true)) { wx.getString("temp", weather, sizeof(weather)); wx.end(); } }
  command.reserve(170); paint();
}
uint32_t touchReadUs = 0;   // how long reading the touchscreen takes, smoothed
void loop() {
  { const uint32_t began = micros(); M5.update(); const uint32_t took = micros() - began; touchReadUs = touchReadUs ? (touchReadUs * 7 + took) / 8 : took; }
  auto t=M5.Touch.getDetail();
  bool tap=t.wasPressed() && t.y>=182 && t.y<=226 && t.x>=10 && t.x<=309;
  // A double-press on the stick is the photobooth's remote shutter, from any screen.
  {
    wand::Poke poke;
    if (radio::take(wand::kPoke, poke)) {
      radio::unpair();                       // keep broadcasting so the stick always hears feeds
      const bool fresh = !pokeKnown || poke.seq != pokeSeen;
      pokeKnown = true; pokeSeen = poke.seq;
      if (fresh) { takePhoto(); return; }
    }
  }
  if (mode==Mode::Japanese) {
    if (arcade::update(t.wasPressed(),t.x,t.y)) { arcade::leave(); change(Mode::JapaneseMenu); }
  } else if (mode==Mode::Kanji) {
    if (kanji::update()) change(Mode::JapaneseMenu);
  } else if (mode==Mode::JapaneseMenu && t.wasPressed()) {
    if (tap && t.x<130) change(Mode::Choose);
    else if (t.y>=72 && t.y<122) talkWithLuna();
    else if (t.y>=124 && t.y<174 && t.x<160) { voiceNote[0]=0; mode=Mode::Japanese; arcade::enter(); }
    else if (t.y>=124 && t.y<174) { voiceNote[0]=0; mode=Mode::Kanji; kanji::enter(); }
  } else if (mode==Mode::Booth && t.wasPressed()) {
    // Back returns to whatever screen the shutter interrupted.
    auto back = [&]() {
      if (boothFrom==Mode::Japanese) { mode=Mode::Japanese; arcade::paint(); }
      else if (boothFrom==Mode::Kanji) { mode=Mode::Kanji; kanji::paint(); }
      else change(boothFrom==Mode::Booth ? Mode::Choose : boothFrom);
    };
    if (booth::hasPhoto) { if (t.y>=170 && t.x<161) takePhoto(); else if (t.y>=170) back(); }
    else if (tap && t.x<126) change(Mode::Choose);
    else if (tap) takePhoto();
  } else if (mode==Mode::Quote && t.wasPressed()) {
    if (tap && t.x<126) change(Mode::Choose);
    else { nextQuote(); paint(); earn(0, 5); }
  } else if (mode==Mode::Work && t.wasPressed()) {
    if (tap) change(Mode::Choose);
    else if(t.x>=10 && t.x<310 && t.y>=76 && t.y<168) {
      const int i=std::min(2,(t.y-76)/31);
      if (tasks[i][0] && strcmp(tasks[i],"-")!=0) {
        done[i]=!done[i]; paint();
        if (done[i]) {                       // the ticked task turns into a star that flies off to Luna
          earn(2, 2);
          for (int step=0; step<10; ++step) {
            const int x=14+step*29, y=76+i*31-step*step/2-6;
            img(art::starFly[step%4], 40, 40, x, std::max(0,y));
            delay(34);
          }
          paint();
        }
        Serial.printf("LUNA/1 TASKDONE %d %d %s\n",i,done[i]?1:0,tasks[i]);
      }
    }
  } else if (mode==Mode::Choose && t.wasPressed()) {
    // Large tiles below the painting: Work and Japanese, then Quotes across the bottom.
    // (Voice-note recording is retired from the home screen.)
    if (t.y>=122) {                       // show the pressed plaque for a moment
      tile((t.y>=181 ? 2 : 0) + (t.x>=160 ? 1 : 0), true);
      delay(110);
    }
    if (t.y<122) {}
    else if (t.y>=181 && t.x>=160) takePhoto();
    else if (t.y>=181) { nextQuote(); change(Mode::Quote); earn(0, 5); }
    else if (t.x<160) change(Mode::Work);
    else change(Mode::JapaneseMenu);
  } else if (mode==Mode::Ready && tap) {
    if (t.x<95) change(Mode::Choose);
    else if (t.x>=102) {
      M5.Speaker.end(); count=0;
      change(M5.Mic.begin()?Mode::Recording:Mode::Fault);
    }
  } else if (mode==Mode::Recording) {
    if (tap || count>=capacity) finish();
    else {
      if (M5.Mic.isRecording()<2) {
        if (M5.Mic.record(samples+count, block, rate, false)) count+=block;
        else { M5.Mic.end(); change(Mode::Fault); }
      }
      if (mode==Mode::Recording && millis()-lastPaint>120) {
        lastPaint=millis();
        M5.Display.fillRect(24,149,(count*270)/capacity,4,ink);
      }
    }
  } else if (mode==Mode::Review && tap) {
    if (t.x<156 && M5.Speaker.begin()) {
      M5.Speaker.setVolume(140);
      if (M5.Speaker.playRaw(samples,count,rate,false)) change(Mode::Playing);
    } else if (t.x>=164 && saved) change(Mode::Choose);
  } else if (mode==Mode::Playing && !M5.Speaker.isPlaying()) {
    M5.Speaker.end(); change(Mode::Review);
  }
  // Feed Pocket Luna.
  if (kanji::detail::state().moons>kanjiMoonsSeen) earn(3*(kanji::detail::state().moons-kanjiMoonsSeen), 1);
  else if (kanji::detail::state().tries>kanjiTriesSeen && kanji::detail::state().moons==kanjiMoonsSeen) earn(0, 7);
  kanjiMoonsSeen=kanji::detail::state().moons; kanjiTriesSeen=kanji::detail::state().tries;
  {
    const auto& quiz=arcade::detail::game();
    const bool feedback=mode==Mode::Japanese && quiz.stage==arcade::detail::Stage::Feedback;
    if (quiz.score>quizScoreSeen) earn(quiz.score-quizScoreSeen, 3);
    else if (feedback && !quizWasFeedback) earn(0, 7);        // answered, but not right this time
    quizScoreSeen=quiz.score; quizWasFeedback=feedback;
  }
  if (millis()-lastFeedCast>=2000 || !lastFeedCast) {
    lastFeedCast=millis();
    wand::Feed f={}; wand::stampHeader(f, wand::kFeed); f.total=petFood; f.event=petEvent; f.kind=petKind;
    radio::send(f);
  }
  // A Moon Prism sticker held to the RFID2 reader opens kanji practice from any menu.
  if (mode==Mode::Choose || mode==Mode::Work || mode==Mode::JapaneseMenu || mode==Mode::Quote) {
    char uid[21];
    if (arcade_rfid::poll(uid)) {
      Serial.printf("LUNA/1 TAG %s\n", uid);
      earn(1, 6);
      transform("Kanji practice");
      mode=Mode::Kanji; kanji::enter();
    }
  }
  while (Serial.available()) {
    char c=Serial.read();
    if (c=='\n') { command.trim(); usbCommand(); command=""; }
    else if (command.length()<165) command+=c;
  }
  // Stay silent while the finger is drawing so strokes are never interrupted.
  if (Serial && millis()-lastOffer>=1600 && !(mode==Mode::Kanji && t.isPressed())) {
    lastOffer=millis();
    if (mode==Mode::Review && !saved) Serial.printf("LUNA/1 OFFER %s\n",clipId);
    else Serial.printf("LUNA/1 READY touch=%luus\n", (unsigned long)touchReadUs);
  }
  if (mode!=Mode::Kanji) delay(1);
}
