/*
 * TestUI.ino — Sketch de TEST pentru Easy Web Remote Control v4
 *
 * Scop: demonstrează personalizarea COMPLETĂ a interfeței prin API.
 * Nu folosește hardware (fără DAC / motoare) — doar Serial.println la fiecare
 * comandă, ca să poată fi testat pe ORICE placă ESP32 (clasic, S2, S3, C3).
 *
 * Ce demonstrează:
 *   - butoane cu text și culori diferite, puse pe rânduri diferite
 *   - background-ul paginii schimbat
 *   - titlu, font și culoare text personalizate
 *   - slider personalizat
 *   - callback-uri care dau print la front / back / left / right / stop
 *
 * După upload: conectează-te la rețeaua Wi-Fi "TestRemote" (parola: 12345678),
 * deschide în browser http://192.168.4.1 și urmărește Serial Monitor (115200).
 */

#include <EasyWebRemoteControl.h>

EasyWebRemoteControl remote;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== Test UI Easy Web Remote Control ===");

  // ---------- STILIZARE GLOBALĂ A PAGINII ----------
  remote.setPageTitle("Panou de Test");          // titlul paginii (<h2> + tab title)
  remote.setBackgroundColor("#0f1c3f");          // fundal albastru închis
  remote.setFontFamily("'Segoe UI', Arial, sans-serif");
  remote.setDefaultTextColor("white");           // text alb pe butoane (implicit)

  // ---------- BUTOANE PE RÂNDURI DIFERITE ----------
  // addButton(id, text_afisat, comanda_trimisa, rand)
  remote.addButton("front", "INAINTE", "front", 0);   // rândul 0 (sus)
  remote.addButton("left",  "STANGA",  "left",  1);   // rândul 1 (mijloc)
  remote.addButton("stop",  "STOP",    "stop",  1);   // rândul 1
  remote.addButton("right", "DREAPTA", "right", 1);   // rândul 1
  remote.addButton("back",  "INAPOI",  "back",  2);   // rândul 2 (jos)

  // ---------- CULORI DIFERITE PENTRU FIECARE BUTON ----------
  // setButtonColor(id, culoare_normala, culoare_apasat)
  remote.setButtonColor("front", "#2ecc71", "#27ae60");  // verde
  remote.setButtonColor("back",  "#3498db", "#2980b9");  // albastru
  remote.setButtonColor("left",  "#f39c12", "#d68910");  // portocaliu
  remote.setButtonColor("right", "#9b59b6", "#8e44ad");  // mov
  remote.setButtonColor("stop",  "#e74c3c", "#c0392b");  // roșu

  // STOP rotund, ca să iasă în evidență
  remote.setButtonBorderRadius("stop", "50%");

  // Mărim puțin fontul pe butoanele cu text (textul e mai lung decât săgețile)
  remote.setButtonFontSize("front", 20);
  remote.setButtonFontSize("back",  20);
  remote.setButtonFontSize("left",  20);
  remote.setButtonFontSize("right", 20);
  remote.setButtonFontSize("stop",  22);

  // ---------- SLIDER PERSONALIZAT ----------
  remote.showSlider(true);
  remote.setSliderRange(0, 100);                 // 0..100 (procent)
  remote.setSliderLabel("Viteza (%)");
  remote.setSliderWidth(260);
  remote.setInitialPWM(50);

  // ---------- CALLBACK-URI: doar print pe Serial ----------
  remote.onFront([](){ Serial.println(">> FRONT  (inainte)"); });
  remote.onBack ([](){ Serial.println(">> BACK   (inapoi)");  });
  remote.onLeft ([](){ Serial.println(">> LEFT   (stanga)");  });
  remote.onRight([](){ Serial.println(">> RIGHT  (dreapta)"); });
  remote.onStop ([](){ Serial.println(">> STOP");             });

  // ---------- PORNIRE ÎN MOD ACCESS POINT ----------
  remote.beginAP("TestRemote", "12345678");
}

void loop() {
  remote.update();   // întreținere WebSocket + auto-recovery + watchdog
}
