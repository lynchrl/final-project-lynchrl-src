// Simple service to periodically read the temperature, pressure, and humidity from a
// BME280 sensor (at /dev/bme280) and log the values with a timestamp to a file.
//
// The format of data returned by a read from /dev/bme280 is:
//
//	"T:xx.xx,P:xxx.xx,H:xx\n"
//
// T is the temperature in degrees Celsius, P is the pressure in hPa, and H is the humidity in %RH.
package main

import (
	"bufio"
	"fmt"
	"log"
	"os"
	"time"

	// "service/bme280"
	"github.com/lynchrl/final-project-lynchrl-src/service/bme280"
)

const (
	bme280Device = "/tmp/bme280"
	logFilePath  = "/tmp/bme280.log"
)

func main() {
	// Open the BME280 device for reading.
	// TODO(lynchrl/final-project-lynchrl#11): Replace with os.Open on the actual device.
	bmeDevice, err := bme280.Open(bme280Device)
	if err != nil {
		log.Fatal(err)
	}
	defer bmeDevice.Close()

	// Open the log file for appending.
	logFile, err := os.OpenFile(logFilePath, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0644)
	if err != nil {
		log.Fatal(err)
	}
	defer logFile.Close()

	// Loop forever, reading from the BME280 and writing to the log file every 10 seconds.
	for {
		// Read a line from the BME280 device.
		scanner := bufio.NewScanner(bmeDevice)
		if scanner.Scan() {
			line := scanner.Text()
			// Get the current timestamp.
			timestamp := time.Now().Format(time.RFC3339)
			// Write the timestamp and sensor data to the log file.
			logLine := fmt.Sprintf("%s %s\n", timestamp, line)
			if _, err := logFile.WriteString(logLine); err != nil {
				log.Printf("Error writing to log file: %v", err)
			}
		} else if err := scanner.Err(); err != nil {
			log.Printf("Error reading from BME280: %v", err)
		}

		// Wait for 10 seconds before the next read.
		time.Sleep(10 * time.Second)
		bmeDevice.Seek(0, 0) // Reset the file offset to the beginning for the next read.
	}
}
