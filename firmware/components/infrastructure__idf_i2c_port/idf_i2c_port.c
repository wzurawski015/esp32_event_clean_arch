esp_err_t i2c_bus_probe_addr(i2c_bus_t* bus, uint8_t addr7,
                             uint32_t timeout_ms, bool* out_ack)
{
    if (!bus || !out_ack) return ESP_ERR_INVALID_ARG;
    // Nowy driver ESP-IDF: szybkie sprawdzenie ACK bez dodawania urządzenia
    esp_err_t err = i2c_master_probe(bus->hbus, addr7, (int)timeout_ms);
    if (err == ESP_OK) {
        *out_ack = true;    // ACK
        return ESP_OK;
    }
    if (err == ESP_ERR_TIMEOUT) {
        return err;         // realny błąd magistrali
    }
    *out_ack = false;       // brak ACK -> traktujemy jako "nie ma urządzenia"
    return ESP_OK;
}
