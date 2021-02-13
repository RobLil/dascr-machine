package ui

import (
	"context"
	"embed"
	"fmt"
	"html/template"
	"io/fs"
	"io/ioutil"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/dascr/dascr-machine/service/config"
	"github.com/dascr/dascr-machine/service/connector"
	"github.com/dascr/dascr-machine/service/logger"
)

// Static will provide the embedded files as http.FS
//go:embed static
var webui embed.FS

// WebServer will hold the information to run the UIs webserver
type WebServer struct {
	IP         string
	Port       int
	HTTPServer *http.Server
}

// templateOptions will hold information to render the page
type templateOptions struct {
	Config     *config.Settings
	SerialList []string
}

// New will return an instantiated WebServer
func New() *WebServer {
	var ip string
	var err error
	// Read eth name from ENV
	netInt := config.MustGet("INT")
	if netInt != "any" {
		ip, err = config.ReadSystemIP(netInt)
		if err != nil {
			logger.Panic(err)
		}
	} else {
		ip = "0.0.0.0"
	}

	return &WebServer{
		IP:   ip,
		Port: 3000,
	}
}

// Start will start the ui webserver
func (ws *WebServer) Start() error {
	staticdir, err := fs.Sub(webui, "static")
	if err != nil {
		return err
	}
	// Setup routing
	http.Handle("/", http.FileServer(http.FS(staticdir)))
	http.HandleFunc("/admin", ws.admin)
	http.HandleFunc("/updateMachine", ws.updateMachine)
	http.HandleFunc("/updateScoreboard", ws.updateScoreboard)
	add := fmt.Sprintf("%+v:%+v", ws.IP, ws.Port)
	ws.HTTPServer = &http.Server{
		Addr: add,
	}
	logger.Infof("Navigate to http://%+v:%+v to configure your darts machine", ws.IP, ws.Port)

	if err := ws.HTTPServer.ListenAndServe(); err != nil {
		return err
	}

	return nil
}

// Stop will stop the ui webserver
func (ws *WebServer) Stop(ctx context.Context) {
	ws.HTTPServer.Shutdown(ctx)
	logger.Info("Webserver stopped")
}

// Admin route
func (ws *WebServer) admin(w http.ResponseWriter, r *http.Request) {
	file, err := webui.ReadFile("static/admin.html")
	if err != nil {
		logger.Errorf("Error when reading template admin.html %+v", err)
		http.Error(w, "Error when reading template admin.html", http.StatusBadRequest)
		return
	}

	t := template.New("adminPage")
	_, err = t.Parse(string(file))
	if err != nil {
		logger.Errorf("Error when reading template admin.html %+v", err)
		http.Error(w, "Error when reading template admin.html", http.StatusBadRequest)
		return
	}

	tinfo := &templateOptions{
		Config:     &config.Config,
		SerialList: findArduino(),
	}

	err = t.Execute(w, tinfo)
	if err != nil {
		logger.Errorf("Cannot execute template admin.html: %+v", err)
		http.Error(w, "Error when executing template admin.html", http.StatusBadRequest)
		return
	}
}

// Update routes
func (ws *WebServer) updateMachine(w http.ResponseWriter, r *http.Request) {
	err := r.ParseForm()
	if err != nil {
		logger.Error(err)
		http.Error(w, "Error when updating machine settings", http.StatusBadRequest)
		return
	}

	delayTime, err := strconv.Atoi(r.FormValue("delay"))
	if err != nil {
		logger.Errorf("Invalid form input: %+v", err)
		http.Error(w, "Error when updating machine settings", http.StatusBadRequest)
		return
	}

	usThreshold, err := strconv.Atoi(r.FormValue(("thresh")))
	if err != nil {
		logger.Errorf("Invalid form input: %+v", err)
		http.Error(w, "Error when updating machine settings", http.StatusBadRequest)
		return
	}

	if config.Config.Machine.WaitingTime != delayTime {
		// set it in config
		config.Config.Machine.WaitingTime = delayTime
		// Set it for the game object too
		w := time.Second * time.Duration(delayTime)
		d := w + 2*time.Second

		connector.MachineConnector.Game.WaitingTime = w
		connector.MachineConnector.Game.DebounceTime = d
		logger.Debugf("Changed Waiting time and Debounce time of Connector to: %+v and %+v", connector.MachineConnector.Game.WaitingTime, connector.MachineConnector.Game.DebounceTime)
	}
	if config.Config.Machine.Piezo != usThreshold {
		// set it in config
		config.Config.Machine.Piezo = usThreshold
		// Write the Piezo Threshold time to set it at Arduino side
		threshold := fmt.Sprintf("p,%+v", usThreshold)
		connector.MachineConnector.Serial.Write(threshold)
		logger.Debugf("Changed Piezo Threshold to: %+v and wrote it to serial port", config.Config.Machine.Piezo)
	}
	if config.Config.Machine.Serial != r.FormValue("serial") {
		// set it in config
		config.Config.Machine.Serial = r.FormValue("serial")
		logger.Debug("Serial port changed, reloading serial connection")
		// stop serial connection and start it again after sleeping 1 second
		connector.MachineConnector.Serial.Reload()
		logger.Debug("Finished reloading serial connection")
	}

	err = config.SaveConfig()
	if err != nil {
		logger.Errorf("Error writing config file after update: %+v", err)
		http.Error(w, "Error when updating machine settings", http.StatusBadRequest)
		return
	}

	// Redirect back to admin
	time.Sleep(time.Second / 5)
	http.Redirect(w, r, "/admin", http.StatusSeeOther)
}

func (ws *WebServer) updateScoreboard(w http.ResponseWriter, r *http.Request) {
	err := r.ParseForm()
	if err != nil {
		logger.Error(err)
		http.Error(w, "Error when updating scoreborad settings", http.StatusBadRequest)
		return
	}

	// Change config for persistance and MachineConnector.Sender config for live update
	https := r.FormValue("sbprot") != ""
	if config.Config.Scoreboard.HTTPS != https {
		config.Config.Scoreboard.HTTPS = https
		connector.MachineConnector.Sender.HTTPS = https
	}

	if config.Config.Scoreboard.Host != r.FormValue("sbhost") {
		val := r.FormValue("sbhost")
		config.Config.Scoreboard.Host = val
		connector.MachineConnector.Sender.IP = val
	}

	if config.Config.Scoreboard.Port != r.FormValue("sbport") {
		val := r.FormValue("sbport")
		config.Config.Scoreboard.Port = val
		connector.MachineConnector.Sender.Port = val
	}

	if config.Config.Scoreboard.Game != r.FormValue("sbgame") {
		val := r.FormValue("sbgame")
		config.Config.Scoreboard.Game = val
		connector.MachineConnector.Sender.GameID = val
	}

	if config.Config.Scoreboard.User != r.FormValue("sbuser") {
		val := r.FormValue("sbuser")
		config.Config.Scoreboard.User = val
		connector.MachineConnector.Sender.User = val
	}

	if config.Config.Scoreboard.Pass != r.FormValue("sbpass") {
		val := r.FormValue("sbpass")
		config.Config.Scoreboard.Pass = val
		connector.MachineConnector.Sender.Pass = val
	}

	logger.Debug("Scoreboard configuration changed, checking connection...")
	/* Check connection via heartbeat */
	if err := connector.MachineConnector.Sender.CheckConnection(); err != nil {
		config.Config.Scoreboard.Error = err.Error()
	} else {
		config.Config.Scoreboard.Error = ""
	}

	logger.Debug("Reloading websocket")
	/* Reconnect websocket */
	connector.MachineConnector.Websocket.Reload()
	logger.Debug("Finished reloading websocket config")

	err = config.SaveConfig()
	if err != nil {
		logger.Errorf("Error writing config file after update: %+v", err)
		http.Error(w, "Error when updating scoreborad settings", http.StatusBadRequest)
		return
	}

	// Redirect back to admin
	time.Sleep(time.Second / 5)
	http.Redirect(w, r, "/admin", http.StatusSeeOther)
}

// findArduino looks for the file that represents the Arduino
// serial connection. Returns the fully qualified path to the
// ui template if we are able to find a likely candidate for an
// Arduino, otherwise an error string if unable to find
// something that 'looks' like an Arduino device.
func findArduino() []string {
	var list []string
	contents, _ := ioutil.ReadDir("/dev")

	// Look for what is mostly likely the Arduino device
	for _, f := range contents {
		if strings.Contains(f.Name(), "tty.usbserial") ||
			strings.Contains(f.Name(), "ttyUSB") || strings.Contains(f.Name(), "ttyACM") {
			list = append(list, "/dev/"+f.Name())
		}
	}

	// If the list has content return the list
	// Otherwise return one single entry
	if len(list) != 0 {
		return list
	}
	list = append(list, "Arduino not found")
	return list
}
