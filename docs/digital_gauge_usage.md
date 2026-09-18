# Digital gauge reader

`CVSDK_DigitalGaugeReader` reads calibrated red/green seven-segment displays without a neural
network. It is separate from `CVSDK_GaugeReader`, which reads analog pointer gauges using ONNX.

```c
CVSDK_DigitalGaugeReaderOptions options = {
    sizeof(CVSDK_DigitalGaugeReaderOptions), 0.0f, 1, {0}};
CVSDK_DigitalGaugeReader* reader = NULL;
CVSDK_Status status = CVSDK_DigitalGaugeReaderCreate(
    "models/digital_gauge_v1", &options, &reader);

uint32_t count = 0;
status = CVSDK_DigitalGaugeReaderInfer(reader, &image, NULL, 0, &count);
CVSDK_DigitalGaugeReading* rows = calloc(count, sizeof(*rows));
status = CVSDK_DigitalGaugeReaderInfer(reader, &image, rows, count, &count);
CVSDK_DigitalGaugeReaderDestroy(reader);
free(rows);
```

Each output item represents one display row. `panel_index` and `role` associate PV and SV rows
with a panel. Always inspect `confidence`, `segment_specificity`, and `flags`; a decoded value can
still be ambiguous when bloom removes the dark gaps between segments.

The shipped configuration is calibrated for the blue-panel temperature controllers in the
regression data. HSV thresholds and geometric constraints live in `reader_config.json` and should
be validated before using a different device family.
