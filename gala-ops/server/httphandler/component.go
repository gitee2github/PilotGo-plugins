package httphandler

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"gitee.com/openeuler/PilotGo-plugins/sdk/plugin/client"
	"gitee.com/openeuler/PilotGo-plugins/sdk/utils/httputils"
)

type Opsclient struct {
	Sdkmethod   *client.Client
	PromePlugin map[string]interface{}
}

var Galaops *Opsclient

func (o *Opsclient) UnixTimeStartandEnd(timerange time.Duration) (int64, int64) {
	now := time.Now()
	past5Minutes := now.Add(timerange * time.Minute)
	startOfPast5Minutes := time.Date(past5Minutes.Year(), past5Minutes.Month(), past5Minutes.Day(),
		past5Minutes.Hour(), past5Minutes.Minute(), 0, 0, past5Minutes.Location())
	timestamp := startOfPast5Minutes.Unix()
	return timestamp, now.Unix()
}

func (o *Opsclient) QueryMetric(endpoint string, querymethod string, param string) (interface{}, error) {
	ustr := endpoint + "/api/v1/" + querymethod + param
	u, err := url.Parse(ustr)
	if err != nil {
		return nil, err
	}
	u.RawQuery = u.Query().Encode()

	httpClient := &http.Client{Timeout: 10 * time.Second}
	resp, err := httpClient.Get(u.String())
	if err != nil {
		return nil, err
	}
	bs, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}

	defer resp.Body.Close()

	var data interface{}

	err = json.Unmarshal(bs, &data)
	if err != nil {
		return nil, fmt.Errorf("unmarshal cpu usage rate error:%s", err.Error())
	}
	return data, nil
}

func (o *Opsclient) Getplugininfo(pilotgoserver string, pluginname string) (map[string]interface{}, error) {
	resp, err := http.Get(pilotgoserver + "/api/v1/plugins")
	if err != nil {
		return nil, fmt.Errorf("faild to get plugin list: %s", err.Error())
	}
	defer resp.Body.Close()

	var buf bytes.Buffer
	_, erriocopy := io.Copy(&buf, resp.Body)
	if erriocopy != nil {
		return nil, erriocopy
	}

	data := map[string]interface{}{
		"code": nil,
		"data": nil,
		"msg":  nil,
	}
	err = json.Unmarshal(buf.Bytes(), &data)
	if err != nil {
		return nil, fmt.Errorf("unmarshal request plugin info error:%s", err.Error())
	}

	var PromePlugin map[string]interface{}
	for _, p := range data["data"].([]interface{}) {
		if p.(map[string]interface{})["name"] == pluginname {
			PromePlugin = p.(map[string]interface{})
		}
	}
	if len(PromePlugin) == 0 {
		return nil, fmt.Errorf("pilotgo server not add prometheus plugin")
	}
	return PromePlugin, nil
}

func (o *Opsclient) SendJsonMode(jsonmodeURL string) (string, int, error) {
	url := Galaops.PromePlugin["url"].(string) + jsonmodeURL

	_, thisfile, _, _ := runtime.Caller(0)
	dirpath := filepath.Dir(thisfile)
	files := make(map[string]string)
	err := filepath.Walk(path.Join(dirpath, "..", "gui-json-mode"), func(jsonfilepath string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if !info.Mode().IsRegular() {
			return nil
		}
		data, err := os.ReadFile(jsonfilepath)
		if err != nil {
			return err
		}
		_, jsonfilename := filepath.Split(jsonfilepath)
		files[strings.Split(jsonfilename, ".")[0]] = string(data)
		return nil
	})
	if err != nil {
		return "", -1, err
	}

	resp, err := httputils.Post(url, &httputils.Params{
		Body: files,
	})
	if resp != nil {
		if err != nil || resp.StatusCode != 201 {
			return "", resp.StatusCode, err
		}
		return string(resp.Body), resp.StatusCode, nil
	}
	return "the target web server does not exist", -1, err
}

func (o *Opsclient) CheckPrometheusPlugin() (bool, error) {
	url := Galaops.PromePlugin["url"].(string) + "aaa"
	resp, err := httputils.Get(url, nil)
	if resp == nil {
		return false, err
	}
	return true, err
}