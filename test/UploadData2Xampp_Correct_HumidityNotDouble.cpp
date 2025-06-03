// === Enhanced Data Upload Functions ===
void UploadData2Xampp(int id) {
    Serial.println("Uploading data to server...");

    // Format decimal values to ensure consistent formatting with 2 decimal places
    char tempStr[10];
    char humStr[10];
    
    // Format temperature and humidity to 2 decimal places
    dtostrf(temperatures[id - 1], 6, 2, tempStr);
    dtostrf(humidities[id - 1], 6, 2, humStr);
    
    // Remove leading spaces that dtostrf might add
    String tempFormatted = String(tempStr);
    String humFormatted = String(humStr);
    tempFormatted.trim();
    humFormatted.trim();

    String postData = "read_module_no=" + String(id) + 
                      "&temperature=" + tempFormatted + 
                      "&humidity=" + humFormatted + 
                      "&readingId=" + String(readIds[id - 1]); 

    Serial.println("POST Data: " + postData); // Debug output

    HTTPClient http; 
    http.begin(URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    int httpCode = http.POST(postData); 
    if (httpCode > 0) {
        String payload = http.getString(); 
        Serial.print("HTTP Code: "); Serial.println(httpCode); 
        Serial.print("Response: "); Serial.println(payload); 
    } else {
        Serial.print("HTTP POST Error: "); Serial.println(httpCode); 
    }
    
    http.end();
    readytoupload[id - 1] = false;
    delay(1000);
}

// Alternative method using sprintf for even more control
void UploadData2Xampp_Alternative(int id) {
    Serial.println("Uploading data to server...");

    // Use sprintf for precise formatting
    char postDataBuffer[200];
    snprintf(postDataBuffer, sizeof(postDataBuffer), 
            "read_module_no=%d&temperature=%.2f&humidity=%.2f&readingId=%d",
            id, temperatures[id - 1], humidities[id - 1], readIds[id - 1]);

    String postData = String(postDataBuffer);
    Serial.println("POST Data: " + postData); // Debug output

    HTTPClient http; 
    http.begin(URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    int httpCode = http.POST(postData); 
    if (httpCode > 0) {
        String payload = http.getString(); 
        Serial.print("HTTP Code: "); Serial.println(httpCode); 
        Serial.print("Response: "); Serial.println(payload); 
    } else {
        Serial.print("HTTP POST Error: "); Serial.println(httpCode); 
    }
    
    http.end();
    readytoupload[id - 1] = false;
    delay(1000);
}