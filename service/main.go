// Simple service to periodically read the temperature, pressure, and humidity from a
// BME280 sensor (at /dev/bme280) and log the values with a timestamp to a SQLite3 database.
//
// The format of data returned by a read from /dev/bme280 is:
//
//	"T:xx.xx,P:xxx.xx,H:xx\n"
//
// T is the temperature in degrees Celsius, P is the pressure in hPa, and H is the humidity in %RH.
package main

import (
	"bufio"
	"database/sql"
	"flag"
	"fmt"
	"log"
	"os"
	"strconv"
	"strings"
	"time"

	_ "github.com/mattn/go-sqlite3"
)

var (
	readInterval = flag.Duration("read_interval", 10*time.Second, "Interval between sensor reads")
	devicePath   = flag.String("device_path", "/dev/bme280", "Path to the BME280 device file")
	dbPath       = flag.String("db_path", "/tmp/bme280.db", "Path to the SQLite3 database file")
)

func main() {
	fmt.Println("Starting up BME280 service.")
	flag.Parse()

	// Open SQLite3 database.
	db, err := sql.Open("sqlite3", *dbPath)
	if err != nil {
		log.Fatal(err)
	}
	defer db.Close()

	// Create the table if it doesn't exist.
	createTableSQL := `CREATE TABLE IF NOT EXISTS sensor_readings (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		timestamp DATETIME NOT NULL,
		temperature REAL NOT NULL,
		pressure REAL NOT NULL,
		humidity REAL NOT NULL
	);`
	_, err = db.Exec(createTableSQL)
	if err != nil {
		log.Fatal(err)
	}

	// Loop forever, reading from the BME280 and writing to the database every 10 seconds.
	for {
		bmeDevice, err := os.Open(*devicePath)
		if err != nil {
			log.Fatal(err)
		}
		// Read a line from the BME280 device.
		scanner := bufio.NewScanner(bmeDevice)
		if scanner.Scan() {
			line := scanner.Text()
			// Parse the sensor data.
			temperature, pressure, humidity, err := parseSensorData(line)
			if err != nil {
				log.Printf("Error parsing sensor data: %v", err)
				bmeDevice.Close()
				time.Sleep(*readInterval)
				continue
			}
			// Get the current timestamp.
			timestamp := time.Now().Format(time.RFC3339)
			// Insert into database.
			insertSQL := `INSERT INTO sensor_readings (timestamp, temperature, pressure, humidity) VALUES (?, ?, ?, ?)`
			_, err = db.Exec(insertSQL, timestamp, temperature, pressure, humidity)
			if err != nil {
				log.Printf("Error writing to database: %v", err)
			}
		} else if err := scanner.Err(); err != nil {
			log.Printf("Error reading from BME280: %v", err)
		}

		// Wait for the specified interval before the next read.
		bmeDevice.Close()
		time.Sleep(*readInterval)
	}
}

// parseSensorData parses the sensor data string in the format "T:xx.xx,P:xxx.xx,H:xx"
// and returns the temperature, pressure, and humidity values.
func parseSensorData(data string) (float64, float64, float64, error) {
	parts := strings.Split(strings.TrimSpace(data), ",")
	if len(parts) != 3 {
		return 0, 0, 0, fmt.Errorf("invalid sensor data format: expected 3 parts, got %d", len(parts))
	}

	// Extract temperature
	temp := strings.TrimPrefix(parts[0], "T:")
	temperature, err := strconv.ParseFloat(temp, 64)
	if err != nil {
		return 0, 0, 0, fmt.Errorf("failed to parse temperature: %v", err)
	}

	// Extract pressure
	press := strings.TrimPrefix(parts[1], "P:")
	pressure, err := strconv.ParseFloat(press, 64)
	if err != nil {
		return 0, 0, 0, fmt.Errorf("failed to parse pressure: %v", err)
	}

	// Extract humidity
	hum := strings.TrimPrefix(parts[2], "H:")
	humidity, err := strconv.ParseFloat(hum, 64)
	if err != nil {
		return 0, 0, 0, fmt.Errorf("failed to parse humidity: %v", err)
	}

	return temperature, pressure, humidity, nil
}
