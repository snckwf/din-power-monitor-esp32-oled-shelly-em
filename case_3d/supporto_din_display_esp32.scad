// supporto_din_display_esp32.scad  — rev.9
// Supporto barra DIN 35 mm — 4 moduli (69.8 mm) — altezza magnetotermico (DIN 43880)
//
// rev.9 (2026-07): CORPO rifatto.
//   - finestra 17.6 x 11 nella parete frontale in corrispondenza dello strip a 4 pin
//     saldato sul display, larga abbastanza per far passare le femmine Dupont;
//     resta interamente coperta dalla fascia, dal davanti non si vede nulla
//   - intaglio nel binario destro dell'ESP32 dietro alla finestra, per liberare
//     spazio al connettore
//   - alloggiamento a scorrimento per il buzzer LS3040 (disco D29.3 x 5) sul lato
//     lungo destro, con linguetta elastica di ritegno e 7 fori di uscita sonora
// rev.8  tasca del display allargata (giochi 0.60), spine D2.7.
// rev.7  quote del display corrette sul disegno ufficiale WP013?L102B-01A;
//        finestra grande quanto la viewing area 31.42 x 16.70.
// rev.6  display cambiato da ST7789 1.69" SPI a OLED SH1106 1.3" I2C.
//
// COSA SI RISTAMPA: fascia (rev.8) e corpo (rev.9). Il coperchio e' invariato
// dalla rev.5 e non va ristampato.
//
// DISPLAY — quote da WP013?L102B-01A
//   PCB              35.40 x 33.50 x 1.00
//   vetro OLED       34.50 x 23.00 x 1.50, a 0.45 dai lati e 5.25 dal bordo alto
//   biadesivo schiuma 0.80 fra vetro e PCB -> spessore totale 3.30
//   viewing area     31.42 x 16.70  (e' la finestra della fascia)
//   area attiva      29.42 x 14.70
//   fori             4 x D3.1, a 2.50 da ogni bordo -> interasse 30.40 x 28.50
//   strip 4 pin      passo 2.54, riga a 2.50 dal bordo alto, gruppo centrato;
//                    da davanti, da sinistra: VSS(GND) - VDD - SCK - SDA
//
// MONTAGGIO
//   1. display nella fascia: entra nella tasca, centrato dalle 4 spine, fermato
//      con una goccia di colla a caldo. Lo strip esce dal retro e passa nella
//      finestra del corpo.
//   2. buzzer: si infila da dietro nella sede sul lato destro finche' scatta
//      sotto la linguetta; batte a z 14 e arriva a z 44, il coperchio lo chiude.
//   3. ESP32 nei binari, vite M3 sul boss inferiore.
//   4. fascia sul corpo con 4 viti M3 dall'interno: usare viti a testa cilindrica
//      bassa sulle due di destra (y 32 e 53), la testa passa davanti al buzzer.
//
// STAMPA
//   fascia    a faccia in giu'
//   corpo     a fronte in giu'
//   coperchio piano sul letto, clip verso l'alto
//   Nessun supporto su nessuno dei tre pezzi.

/* [Pezzo da renderizzare] */
parte = "anteprima"; // ["anteprima","corpo","fascia","coperchio"]

/* [Dimensioni generali DIN 43880] */
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

/* [Giochi e lavorazioni della fascia] */
gioco_pcb   = 0.60;
gioco_vetro = 0.60;
spina_d     = 2.70;
fin_extra   = 0.30;
svaso       = 1.20;
gap_retro   = 1.70;
pad_rilievo = 1.00;

/* [Canale fili laterale della fascia -> asola rev.5 del corpo] */
can_x0 = 45.0;
can_x1 = 57.0;
can_y0 = 12.0;
can_y1 = 33.0;
asola_x0 = 49.825;
asola_w  = 9.5;
asola_y0 = 33.0;      // coordinate corpo
asola_h  = 19.0;

/* [Finestra per lo strip a 4 pin del display — rev.9] */
fpin_x0 = 27.0;
fpin_x1 = 44.6;
fpin_y0 = 49.0;       // coordinate corpo
fpin_y1 = 60.0;
tag_x0  = 39.5;       // intaglio nel binario destro
tag_x1  = 45.2;
tag_y0  = 49.0;
tag_y1  = 60.0;
tag_z1  = 20.5;

/* [Buzzer LS3040 — disco D29.3 x 5 con fili — rev.9] */
bz_d      = 29.3;
bz_t      = 5.0;
bz_gd     = 0.7;      // gioco sul diametro
bz_gt     = 0.6;      // gioco sullo spessore
bz_cy     = 41.0;     // centro del disco lungo y
bz_par    = 2.0;      // spessore delle nervature
bz_z_stop = 14.0;     // battuta: lascia libere le teste delle viti fascia
bz_tng_y0 = 35.0;     // fessure che ricavano la linguetta elastica
bz_tng_y1 = 46.0;
bz_tng_z0 = 30.0;
bz_foro_d = 3.2;      // fori di uscita sonora
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

/* [Passacavo alimentazione 5 V] */
cavo_d     = 5.0;
cavo_x     = 55.0;
cavo_z     = 22.0;
cavo_coll  = 4.0;
fasc_w     = 2.2;
fasc_h     = 7.0;
fasc_dx    = 7.0;

/* [Aggancio DIN 35 mm (EN 50022)] */
rail_sp  = 1.4;

/* [Qualita'] */
$fn = 48;

// ---------- derivate (non toccare) ----------
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
// FASCIA  (invariata dalla rev.8)
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
// CORPO  rev.9
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

// sede del buzzer: due guide, una nervatura interna con linguetta elastica,
// due battute che lasciano libere le teste delle viti della fascia
module sede_buzzer() {
    translate([bz_x0-bz_par, bz_y0-bz_par, pf])
        cube([bz_par, bz_slot_d + 2*bz_par, bz_z1-pf]);          // nervatura interna
    translate([bz_x0-bz_par, bz_y0-bz_par, pf])
        cube([bz_par + bz_slot_t, bz_par, bz_z1-pf]);            // guida bassa
    translate([bz_x0-bz_par, bz_y1, pf])
        cube([bz_par + bz_slot_t, bz_par, bz_z1-pf]);            // guida alta
    translate([bz_x0, bz_y0, pf]) cube([bz_slot_t, 4.0, bz_z_stop-pf]);   // battuta 1
    translate([bz_x0, 38.0, pf])  cube([bz_slot_t, 8.0, bz_z_stop-pf]);   // battuta 2
    // dente: sottile in alto (entrata), spesso in basso (ritegno)
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
        // asola rev.5: ci passa il canale fili laterale della fascia
        translate([asola_x0, asola_y0, fascia_spor-1])
            cube([asola_w, asola_h, parete_fr+2]);
        // finestra per lo strip a 4 pin del display
        translate([fpin_x0, fpin_y0, fascia_spor-1])
            cube([fpin_x1-fpin_x0, fpin_y1-fpin_y0, parete_fr+2]);
        // intaglio nel binario destro dietro alla finestra
        translate([tag_x0, tag_y0, pf-0.1])
            cube([tag_x1-tag_x0, tag_y1-tag_y0, tag_z1-pf+0.1]);
        // passacavo alimentazione 5 V + asole per fascetta stringicavo
        translate([cavo_x, -1, cavo_z]) rotate([-90,0,0])
            cylinder(d=cavo_d, h=parete+cavo_coll+2, $fn=32);
        translate([cavo_x-fasc_dx-fasc_w/2, -1, cavo_z-fasc_h/2]) cube([fasc_w, parete+2, fasc_h]);
        translate([cavo_x+fasc_dx-fasc_w/2, -1, cavo_z-fasc_h/2]) cube([fasc_w, parete+2, fasc_h]);
        // fori passanti per le viti della fascia
        for (p = viti_fascia)
            translate([p[0], p[1]+off_f, fascia_spor-1]) cylinder(d=3.4, h=parete_fr+2, $fn=32);
        // feritoie di ventilazione
        for (xv = [12,26,40,54]) translate([xv, alt-parete-1, 30]) cube([8, parete+2, 3]);
        for (xv = [12,26])       translate([xv, -1, 30])           cube([8, parete+2, 3]);
        // fori colonnine coperchio
        for (p = colonnine) translate([p[0], p[1], prof-12]) cylinder(d=2.6, h=12.1, $fn=32);
        // foro del boss inferiore di bloccaggio scheda
        translate([esp_x0+esp_w/2, esp_y0-4, pf-0.1]) cylinder(d=2.6, h=esp_back-pf+0.2, $fn=32);
        // fessure che ricavano la linguetta elastica della sede buzzer
        translate([bz_x0-bz_par-0.1, bz_tng_y0, bz_tng_z0])
            cube([bz_par+0.2, 1.0, bz_z1-bz_tng_z0+0.2]);
        translate([bz_x0-bz_par-0.1, bz_tng_y1, bz_tng_z0])
            cube([bz_par+0.2, 1.0, bz_z1-bz_tng_z0+0.2]);
        // passaggio dei fili del buzzer attraverso la guida alta
        translate([bz_x0-0.2, bz_y1-0.1, 20.0]) cube([bz_slot_t+0.4, bz_par+0.2, 10.0]);
        // fori di uscita sonora sulla parete destra
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
// COPERCHIO con aggancio DIN  (invariato dalla rev.5)
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
if (parte == "corpo")          translate([0,0,-fascia_spor]) corpo();
else if (parte == "fascia")    fascia();
else if (parte == "coperchio") coperchio();
else {
    corpo();
    translate([0, off_f, 0]) fascia();
    translate([0, 0, prof]) coperchio();
}
