// din_enclosure_display_esp32.scad  — rev.9
// 35 mm DIN rail enclosure — 4 modules (69.8 mm) — circuit-breaker height (DIN 43880)
// Parts: "body" (corpo), "bezel" (fascia), "lid" (coperchio)
//
// rev.9 (2026-07): BODY redesigned.
//   - 17.6 x 11 window in the front wall in line with the 4-pin strip
//     soldered to the display, wide enough for Dupont female connectors;
//     it stays fully covered by the bezel, nothing is visible from the front
//   - notch in the ESP32's right rail behind the window, to make room
//     for the connector
//   - sliding seat for the LS3040 buzzer (D29.3 x 5 disc) on the long
//     right side, with a spring retaining tab and 7 sound holes
// rev.8  display pocket enlarged (0.60 clearances), D2.7 locating pins.
// rev.7  display dimensions corrected from the official drawing WP013?L102B-01A;
//        window as large as the 31.42 x 16.70 viewing area.
// rev.6  display changed from 1.69" ST7789 SPI to 1.3" SH1106 I2C OLED.
//
// WHAT TO REPRINT: bezel (rev.8) and body (rev.9). The lid is unchanged
// since rev.5 and does not need reprinting.
//
// DISPLAY — dimensions from WP013?L102B-01A
//   PCB              35.40 x 33.50 x 1.00
//   OLED glass       34.50 x 23.00 x 1.50, 0.45 from the sides and 5.25 from the top edge
//   foam tape        0.80 between glass and PCB -> total thickness 3.30
//   viewing area     31.42 x 16.70  (this is the bezel window)
//   active area      29.42 x 14.70
//   holes            4 x D3.1, 2.50 from each edge -> spacing 30.40 x 28.50
//   4-pin strip      2.54 pitch, row 2.50 from the top edge, group centred;
//                    from the front, from the left: VSS(GND) - VDD - SCK - SDA
//
// ASSEMBLY
//   1. display into the bezel: it goes into the pocket, centred by the 4 pins, held
//      with a drop of hot glue. The strip comes out of the back and passes through
//      the body window.
//   2. buzzer: slide it in from the back into the seat on the right side until it
//      clicks under the tab; it stops at z 14 and reaches z 44, the lid closes it.
//   3. ESP32 in the rails, M3 screw on the lower boss.
//   4. bezel onto the body with 4 M3 screws from the inside: use low cheese-head
//      screws on the two on the right (y 32 and 53), their heads pass in front of the buzzer.
//
// PRINTING
//   bezel     face down
//   body      front face down
//   lid       flat on the bed, clips facing up
//   No supports on any of the three parts.

/* [Part to render] */
parte = "preview"; // ["preview","body","bezel","lid"]

/* [DIN 43880 overall dimensions] */
moduli      = 4;
larg        = moduli*17.5 - 0.2;  // 69.8
alt         = 85;
fascia_alt  = 45;
fascia_spor = 8;
prof        = 45;
parete      = 2;
parete_fr   = 2.5;

/* [Display OLED SH1106 1.3" 128x64 I2C] */
pcb_w      = 35.40;
pcb_h      = 33.50;
pcb_t      = 1.00;
vetro_w    = 34.50;
vetro_h    = 23.00;
vetro_t    = 1.50;
vetro_dx   = 0.45;
vetro_dtop = 5.25;
schiuma_t  = 0.80;
va_w       = 31.42;
va_h       = 16.70;
va_dtop    = 6.35;
att_w      = 29.42;
att_h      = 14.70;
att_dtop   = 7.35;
foro_d     = 3.10;
foro_bordo = 2.50;
pad_dtop   = 2.50;
pad_x1     = 13.89;
pad_passo  = 2.54;
pad_n      = 4;

/* [Bezel clearances and features] */
gioco_pcb   = 0.60;
gioco_vetro = 0.60;
spina_d     = 2.70;
fin_extra   = 0.30;
svaso       = 1.20;
gap_retro   = 1.70;
pad_rilievo = 1.00;

/* [Bezel side wire channel -> rev.5 body slot] */
can_x0 = 45.0;
can_x1 = 57.0;
can_y0 = 12.0;
can_y1 = 33.0;
asola_x0 = 49.825;
asola_w  = 9.5;
asola_y0 = 33.0;      // body coordinates
asola_h  = 19.0;

/* [Window for the display's 4-pin strip — rev.9] */
fpin_x0 = 27.0;
fpin_x1 = 44.6;
fpin_y0 = 49.0;       // body coordinates
fpin_y1 = 60.0;
tag_x0  = 39.5;       // notch in the right rail
tag_x1  = 45.2;
tag_y0  = 49.0;
tag_y1  = 60.0;
tag_z1  = 20.5;

/* [LS3040 buzzer — D29.3 x 5 disc with wires — rev.9] */
bz_d      = 29.3;
bz_t      = 5.0;
bz_gd     = 0.7;      // diameter clearance
bz_gt     = 0.6;      // thickness clearance
bz_cy     = 41.0;     // disc centre along y
bz_par    = 2.0;      // rib thickness
bz_z_stop = 14.0;     // stop: keeps the bezel screw heads clear
bz_tng_y0 = 35.0;     // slots that form the spring tab
bz_tng_y1 = 46.0;
bz_tng_z0 = 30.0;
bz_foro_d = 3.2;      // sound holes
bz_foro_r = 6.5;

/* [ESP32 DevKit 30 pin USB-C — PCBA 28 x 50] */
esp_w      = 28.0;
esp_l      = 50.0;
esp_t      = 1.6;
esp_x0     = 13.0;
esp_y0     = 16.0;
esp_z      = 15.5;
esp_gioco  = 0.20;
esp_presa  = 3.50;

/* [5 V power cable grommet] */
cavo_d     = 5.0;
cavo_x     = 55.0;
cavo_z     = 22.0;
cavo_coll  = 4.0;
fasc_w     = 2.2;
fasc_h     = 7.0;
fasc_dx    = 7.0;

/* [35 mm DIN clip (EN 50022)] */
rail_sp  = 1.4;

/* [Quality] */
$fn = 48;

// ---------- derived (do not touch) ----------
pf       = fascia_spor + parete_fr;       // 10.5
cx       = larg/2;
cyl      = fascia_alt/2;
off_f    = (alt-fascia_alt)/2;            // 20
esp_back = esp_z + esp_t;
esp_lip  = esp_z + 4;

disp_t    = vetro_t + schiuma_t + pcb_t;                // 3.30
fascia_fr = fascia_spor - gap_retro - disp_t;           // 3.00
z_vetro   = fascia_fr;
z_pcb     = fascia_fr + vetro_t + schiuma_t;            // 5.30
z_retro   = z_pcb + pcb_t;                              // 6.30

va_cy_pcb = pcb_h - va_dtop - va_h/2;     // 18.80
pcb_x0 = cx - pcb_w/2;                    // 17.20
pcb_y0 = cyl - va_cy_pcb;                 //  3.70
vetro_x0 = pcb_x0 + vetro_dx;
vetro_y0 = pcb_y0 + (pcb_h - vetro_dtop - vetro_h);
att_x0   = pcb_x0 + (pcb_w - att_w)/2;
att_y0   = pcb_y0 + (pcb_h - att_dtop - att_h);
fin_w = va_w + fin_extra;
fin_h = va_h + fin_extra;
foro_x = [pcb_x0 + foro_bordo, pcb_x0 + pcb_w - foro_bordo];
foro_y = [pcb_y0 + foro_bordo, pcb_y0 + pcb_h - foro_bordo];
pad_y  = pcb_y0 + pcb_h - pad_dtop;
pad_xa = pcb_x0 + pad_x1;
pad_xb = pad_xa + pad_passo*(pad_n - 1);

bz_slot_d = bz_d + bz_gd;                 // 30.0
bz_slot_t = bz_t + bz_gt;                 //  5.6
bz_x1 = larg - parete;                    // 67.8
bz_x0 = bz_x1 - bz_slot_t;                // 62.2
bz_y0 = bz_cy - bz_slot_d/2;              // 26.0
bz_y1 = bz_cy + bz_slot_d/2;              // 56.0
bz_z1 = prof - 1.0;                       // 44.0
bz_cz = bz_z_stop + bz_slot_d/2;          // 29.0

viti_fascia = [[5, 12], [5, 33], [63.5, 12], [63.5, 33]];
colonnine   = [[6.5,6.5],[larg-6.5,6.5],[6.5,alt-6.5],[larg-6.5,alt-6.5]];

// =====================================================
// BEZEL (fascia)  (unchanged since rev.8)
// =====================================================
module fascia() {
  union() {
    difference() {
      cube([larg, fascia_alt, fascia_spor]);
      translate([cx, cyl, 0]) {
        hull() {
          translate([-fin_w/2 - svaso, -fin_h/2 - svaso, -0.01])
            cube([fin_w + 2*svaso, fin_h + 2*svaso, 0.02]);
          translate([-fin_w/2, -fin_h/2, svaso])
            cube([fin_w, fin_h, 0.02]);
        }
        translate([-fin_w/2, -fin_h/2, svaso - 0.05])
          cube([fin_w, fin_h, fascia_fr]);
      }
      translate([vetro_x0 - gioco_vetro/2, vetro_y0 - gioco_vetro/2, z_vetro])
        cube([vetro_w + gioco_vetro, vetro_h + gioco_vetro, vetro_t + schiuma_t]);
      translate([pcb_x0 - gioco_pcb/2, pcb_y0 - gioco_pcb/2, z_pcb])
        cube([pcb_w + gioco_pcb, pcb_h + gioco_pcb, fascia_spor]);
      translate([pad_xa - 2.5, pad_y - 2.1, z_pcb - pad_rilievo])
        cube([(pad_xb - pad_xa) + 5.0, 5.0, pad_rilievo + 0.01]);
      translate([can_x0, can_y0, z_retro])
        cube([can_x1 - can_x0, can_y1 - can_y0, fascia_spor]);
      for (p = viti_fascia)
        translate([p[0], p[1], 2.0]) cylinder(d=2.6, h=fascia_spor, $fn=32);
    }
    for (fx = foro_x) for (fy = foro_y)
      translate([fx, fy, z_pcb]) cylinder(d=spina_d, h=pcb_t, $fn=32);
  }
}

// =====================================================
// BODY (corpo)  rev.9
// =====================================================
module binario_sx() {
    translate([esp_x0-3.6, esp_y0, pf]) cube([4.6, esp_l, esp_z-pf]);
    translate([esp_x0-3.6, esp_y0, esp_z]) cube([3.6-esp_gioco, esp_l, esp_lip-esp_z]);
}
module binario_dx() {
    translate([2*esp_x0 + esp_w, 0, 0]) mirror([1,0,0]) binario_sx();
}
module dente_superiore() {
    translate([esp_x0+esp_w/2-5, esp_y0+esp_l, pf]) cube([10, 3, esp_back+2.5-pf]);
    translate([esp_x0+esp_w/2-5, esp_y0+esp_l-esp_presa, esp_back])
        cube([10, esp_presa, 2.5]);
}

// buzzer seat: two guides, an inner rib with a spring tab,
// two stops that keep the bezel screw heads clear
module sede_buzzer() {
    translate([bz_x0-bz_par, bz_y0-bz_par, pf])
        cube([bz_par, bz_slot_d + 2*bz_par, bz_z1-pf]);          // inner rib
    translate([bz_x0-bz_par, bz_y0-bz_par, pf])
        cube([bz_par + bz_slot_t, bz_par, bz_z1-pf]);            // lower guide
    translate([bz_x0-bz_par, bz_y1, pf])
        cube([bz_par + bz_slot_t, bz_par, bz_z1-pf]);            // upper guide
    translate([bz_x0, bz_y0, pf]) cube([bz_slot_t, 4.0, bz_z_stop-pf]);   // stop 1
    translate([bz_x0, 38.0, pf])  cube([bz_slot_t, 8.0, bz_z_stop-pf]);   // stop 2
    // tooth: thin at the top (entry), thick at the bottom (retention)
    hull() {
        translate([bz_x0, 37.0, 44.0]) cube([0.15, 8.0, 0.5]);
        translate([bz_x0, 37.0, 42.0]) cube([1.20, 8.0, 1.2]);
    }
}

module corpo() {
    difference() {
        union() {
            difference() {
                translate([0,0,fascia_spor]) cube([larg, alt, prof-fascia_spor]);
                translate([parete, parete, pf]) cube([larg-2*parete, alt-2*parete, prof]);
            }
            binario_sx();
            binario_dx();
            dente_superiore();
            translate([esp_x0-1, esp_y0-3, pf]) cube([esp_w+2, 3, esp_back+0.5-pf]);
            translate([esp_x0+esp_w/2, esp_y0-4, pf]) cylinder(d=8, h=esp_back-pf);
            for (p = colonnine) translate([p[0], p[1], pf]) cylinder(d=7, h=prof-pf);
            translate([2,2,pf])              cube([4.5,4.5,prof-pf]);
            translate([larg-6.5,2,pf])       cube([4.5,4.5,prof-pf]);
            translate([2,alt-6.5,pf])        cube([4.5,4.5,prof-pf]);
            translate([larg-6.5,alt-6.5,pf]) cube([4.5,4.5,prof-pf]);
            translate([cavo_x, parete, cavo_z]) rotate([-90,0,0])
                cylinder(d=cavo_d+4, h=cavo_coll, $fn=32);
            sede_buzzer();
        }
        // rev.5 slot: the bezel's side wire channel passes through it
        translate([asola_x0, asola_y0, fascia_spor-1])
            cube([asola_w, asola_h, parete_fr+2]);
        // window for the display's 4-pin strip
        translate([fpin_x0, fpin_y0, fascia_spor-1])
            cube([fpin_x1-fpin_x0, fpin_y1-fpin_y0, parete_fr+2]);
        // notch in the right rail behind the window
        translate([tag_x0, tag_y0, pf-0.1])
            cube([tag_x1-tag_x0, tag_y1-tag_y0, tag_z1-pf+0.1]);
        // 5 V power cable grommet + slots for a cable tie
        translate([cavo_x, -1, cavo_z]) rotate([-90,0,0])
            cylinder(d=cavo_d, h=parete+cavo_coll+2, $fn=32);
        translate([cavo_x-fasc_dx-fasc_w/2, -1, cavo_z-fasc_h/2]) cube([fasc_w, parete+2, fasc_h]);
        translate([cavo_x+fasc_dx-fasc_w/2, -1, cavo_z-fasc_h/2]) cube([fasc_w, parete+2, fasc_h]);
        // through holes for the bezel screws
        for (p = viti_fascia)
            translate([p[0], p[1]+off_f, fascia_spor-1]) cylinder(d=3.4, h=parete_fr+2, $fn=32);
        // ventilation slots
        for (xv = [12,26,40,54]) translate([xv, alt-parete-1, 30]) cube([8, parete+2, 3]);
        for (xv = [12,26])       translate([xv, -1, 30])           cube([8, parete+2, 3]);
        // lid post holes
        for (p = colonnine) translate([p[0], p[1], prof-12]) cylinder(d=2.6, h=12.1, $fn=32);
        // hole of the lower board-locking boss
        translate([esp_x0+esp_w/2, esp_y0-4, pf-0.1]) cylinder(d=2.6, h=esp_back-pf+0.2, $fn=32);
        // slots that form the spring tab of the buzzer seat
        translate([bz_x0-bz_par-0.1, bz_tng_y0, bz_tng_z0])
            cube([bz_par+0.2, 1.0, bz_z1-bz_tng_z0+0.2]);
        translate([bz_x0-bz_par-0.1, bz_tng_y1, bz_tng_z0])
            cube([bz_par+0.2, 1.0, bz_z1-bz_tng_z0+0.2]);
        // buzzer wire passage through the upper guide
        translate([bz_x0-0.2, bz_y1-0.1, 20.0]) cube([bz_slot_t+0.4, bz_par+0.2, 10.0]);
        // sound holes in the right wall
        translate([larg-parete-1, bz_cy, bz_cz]) rotate([0,90,0])
            cylinder(d=bz_foro_d, h=parete+2, $fn=32);
        for (a = [0:5])
            translate([larg-parete-1,
                       bz_cy + bz_foro_r*cos(60*a),
                       bz_cz + bz_foro_r*sin(60*a)]) rotate([0,90,0])
                cylinder(d=bz_foro_d, h=parete+2, $fn=32);
    }
}

// =====================================================
// LID (coperchio) with DIN clip  (unchanged since rev.5)
// =====================================================
module coperchio() {
    t = 2.5;
    zc = t + rail_sp;
    difference() {
        union() {
            cube([larg, alt, t]);
            translate([10,   60.4, t]) cube([15, 4.6, 3.5]);
            translate([44.8, 60.4, t]) cube([15, 4.6, 3.5]);
            translate([10,   58.8, zc]) cube([15, 1.6, t+3.5-zc]);
            translate([44.8, 58.8, zc]) cube([15, 1.6, t+3.5-zc]);
            translate([8,    16, t]) cube([5, 6.6, 3.5]);
            translate([56.8, 16, t]) cube([5, 6.6, 3.5]);
            translate([13,   21, t]) cube([43.8, 1.6, 3.5]);
            translate([29.9, 22.6, t]) cube([10, 2.6, 3.5]);
            hull() {
                translate([29.9, 25.2, zc]) cube([10, 1.0, 0.4]);
                translate([29.9, 25.1, zc]) cube([10, 0.1, t+3.5-zc]);
            }
            translate([30, 12, t]) cube([10, 9.0, 3.5]);
        }
        translate([34.9, 15.5, -1]) cylinder(d=4, h=10, $fn=32);
        for (p = colonnine) translate([p[0], p[1], -1]) cylinder(d=3.4, h=t+2, $fn=32);
        for (xv = [15,25,35,45,55]) translate([xv-1, 67, -1]) cube([2, 10, t+2]);
        for (xv = [18,26,42,50])    translate([xv-1, 4,  -1]) cube([2, 8,  t+2]);
    }
}

// =====================================================
if (parte == "body")           translate([0,0,-fascia_spor]) corpo();
else if (parte == "bezel")     fascia();
else if (parte == "lid")       coperchio();
else {
    corpo();
    translate([0, off_f, 0]) fascia();
    translate([0, 0, prof]) coperchio();
}
