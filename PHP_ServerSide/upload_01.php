<?php
/*************************************************************************************************
 *  Created By: net
 *  Created On: 24/09-07
 *  
 ***********************************************************************************************/
 
// Include database configuration
include 'config.php';

// Check if form data is set
if(isset($_POST["read_module_no"]) && isset($_POST["temperature"]) && isset($_POST["humidity"]) && isset($_POST["readingId"])) {
    
    $rmn = $_POST["read_module_no"];
    $t = $_POST["temperature"];
    $h = $_POST["humidity"];
    $rid = $_POST["readingId"];

    // Prepare an SQL statement
    $stmt = $conn->prepare("INSERT INTO sht30_data (read_module_no, temperature, humidity, readingId) VALUES (?, ?, ?, ?)");
    
    if ($stmt === false) {
        die("Prepare failed: " . htmlspecialchars($conn->error));
    }

    // Bind parameters (i = integer, d = double, s = string, b = BLOB)
    $stmt->bind_param("iddi", $rmn, $t, $h, $rid);
    
    // Execute the statement
    if ($stmt->execute()) { 
        echo "New record created successfully"; 
    } else { 
        echo "Error: " . htmlspecialchars($stmt->error); 
    }

    // Close the statement
    $stmt->close();
}

// Close the connection
$conn->close();
?>