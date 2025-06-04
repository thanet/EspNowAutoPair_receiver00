<?php
/*************************************************************************************************
 *  Database Configuration
 *  Created By: Net
 *  Created On: 2024/9/7
 ***********************************************************************************************/

$hostname = "localhost"; 
$username = "root"; 
$password = ""; 
$database = "esp_data"; // for enjoy desktop
//$database = "esp_data00"; // for the house desktop


// Create connection
$conn = mysqli_connect($hostname, $username, $password, $database);

// Check connection
if (!$conn) { 
    die("Connection failed: " . mysqli_connect_error()); 
} 

// Uncomment the line below if you want to check the connection status
echo "Database connection is OK<br>";
?>
