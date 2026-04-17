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
	"flag"
	"fmt"
	"log"
	"os"
	"time"

	"github.com/lynchrl/final-project-lynchrl-src/service/bme280"
)

const (
	logFilePath = "/tmp/bme280.log"
)

var (
	readInterval = flag.Duration("read_interval", 10*time.Second, "Interval between sensor reads")
	devicePath   = flag.String("device_path", "", "Path to the BME280 device file")
)

type BMEFile interface {
	Read(p []byte) (int, error)
	Seek(offset int64, whence int) (int64, error)
	Close() error
}

func main() {
	flag.Parse()

	var bmeDevice BMEFile
	var err error
	if *devicePath == "" {
		bmeDevice, err = bme280.Open("")
	} else {
		bmeDevice, err = os.Open(*devicePath)
	}
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

		// Wait for the specified interval before the next read.
		time.Sleep(*readInterval)
		bmeDevice.Seek(0, 0) // Reset the file offset to the beginning for the next read.
	}
}
