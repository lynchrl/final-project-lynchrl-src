package main

import (
	"database/sql"
	"encoding/json"
	"log"
	"net/http"
	"path/filepath"
)

type SensorData struct {
	Timestamp   string  `json:"timestamp"`
	Temperature float64 `json:"temperature"`
	Pressure    float64 `json:"pressure"`
	Humidity    float64 `json:"humidity"`
}

func startWebServer(db *sql.DB, httpPort, servRoot string) {
	// Serve the static HTML file
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		http.ServeFile(w, r, filepath.Join(servRoot, "index.html"))
	})

	// Serve the DB data as a JSON
	http.HandleFunc("/api/data", func(w http.ResponseWriter, r *http.Request) {
		// TODO: Limit number of rows returned or filter by date range.
		rows, err := db.Query("SELECT timestamp, temperature, pressure, humidity FROM sensor_readings ORDER BY timestamp ASC")
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		defer rows.Close()

		var data []SensorData
		for rows.Next() {
			var d SensorData
			if err := rows.Scan(&d.Timestamp, &d.Temperature, &d.Pressure, &d.Humidity); err != nil {
				continue
			}
			data = append(data, d)
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(data)
	})

	log.Printf("Web server listening on port %s", httpPort)
	if err := http.ListenAndServe(":"+httpPort, nil); err != nil {
		log.Fatalf("Failed to start web server: %v", err)
	}
}
