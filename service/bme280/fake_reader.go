// Package bme280 provides a fake reader for the BME280 sensor device commonly backed by /dev/bme280.
//
// This is used for testing the main service without needing actual hardware.
package bme280

// FakeBME280File is a fake implementation of the BME280 sensor device file.
type FakeBME280File struct {
}

// NewFakeReader creates a new fake reader for the BME280 sensor.
func Open(filePath string) (*FakeBME280File, error) {
	return &FakeBME280File{}, nil
}

func (fr *FakeBME280File) Read(p []byte) (int, error) {

	ret := "T:25.00,P:1013.25,H:40\n"
	return copy(p, []byte(ret)), nil
}

func (*FakeBME280File) Seek(offset int64, whence int) (int64, error) {
	return 0, nil
}

func (fr *FakeBME280File) Close() error {
	return nil
}
