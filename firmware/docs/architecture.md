# Architektura: Clean + Event-Driven

Warstwy i zależności: `domain ← application ← ports ← infrastructure/adapters ← drivers (ESP-IDF)`.  
Entry point (projekty w `projects/`) jest konsumentem usług i eventów.

```dot
digraph Arch {
  rankdir=LR; node[shape=box, fontname="Helvetica"];
  subgraph cluster_app { label="projects/<app>"; color="#cccccc"; APP; }
  subgraph cluster_domain { label="domain"; color="#cccccc"; DOMAIN; }
  subgraph cluster_application { label="application"; color="#cccccc"; APPLICATION; }
  subgraph cluster_ports { label="ports"; color="#cccccc"; PORTS; }
  subgraph cluster_infra { label="infrastructure (adapters)"; color="#cccccc"; INFRA; }
  subgraph cluster_drivers { label="drivers (ESP-IDF)"; color="#cccccc"; DRV_IDF; }

  APP -> APPLICATION -> PORTS -> INFRA -> DRV_IDF;
  APPLICATION -> DOMAIN;
}


### C. Kconfig komponentu sterownika (usuń duplikaty definicji)

Masz same komentarze — to wystarczy. Jeśli chcesz „czytelne” puste menu, użyj:

**`firmware/components/drivers__lcd1602rgb_dfr_async/Kconfig`**
```kconfig
menu "DFR0464 LCD 16x2 RGB driver"
    comment "Ten komponent nie definiuje opcji Kconfig. Użyj: projects/demo_lcd_rgb/main/Kconfig.projbuild"
endmenu

