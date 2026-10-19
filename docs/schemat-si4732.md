# Schemat połączeń SI4732-A10 <-> ESP32-S3

## SI4732-A10 (SOIC-16)

```
┌──────────────────────┐
│ SENB  ──► VCC  (0x11)│   Adres I2C 0x11 (gdy SENB=VCC)
│ SDA   ──► GPIO  8    │   I2C magistrala (wspólna z Arduino VFD)
│ SCL   ──► GPIO  9    │
│ RST   ──► GPIO 38    │   Active LOW, pull-up 10kΩ
│ LOUT  ──► TS5A23157  │   Analog audio (zawsze analog!)
│ ROUT  ──► TS5A23157  │
│ (I2S DCLK/DFS/DIO)   │   NIE UŻYWANE — patch SSB wyłącza I2S
└──────────────────────┘
```

## ESP32-S3

```
GPIO 8  ──► SDA  (SI4732 + Arduino Pro Mini 0x27)
GPIO 9  ──► SCL
GPIO 38 ──► SI4732 RESET (active LOW)
GPIO 18 ──► TS5A23157 SEL (LOW=PCM5102A / HIGH=SI4732 analog)
```

## TS5A23157 (przełącznik audio 2:1)

```
IN1 (PCM5102A I2S) ──► SI4732 analog (LOUT/ROUT) ──► OUT ──► Wzmacniacz TPA3116
SEL = GPIO 18
```

## Uwagi

- SI4732 **zawsze** analog (LOUT/ROUT) — patch SSB wyłącza I2S
- I2S (GPIO 15/16/17 → PCM5102A) służy **tylko** radia internetowego
- Magistrala I2C (GPIO 8/9) dzielona: SI4732 (0x11) + Arduino VFD (0x27)