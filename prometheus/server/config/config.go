package config

import (
	"fmt"
	"io/ioutil"
	"os"

	"gitee.com/openeuler/PilotGo-plugins/sdk/logger"
	"gopkg.in/yaml.v2"
)

type Prometheus struct {
	URL         string `yaml:"url"`
	ReverseDest string `yaml:"reverseDest"`
}
type HttpConf struct {
	Addr string `yaml:"addr"`
}

type ServerConfig struct {
	Prometheus *Prometheus    `yaml:"prometheus"`
	Http       *HttpConf      `yaml:"http"`
	Logopts    logger.LogOpts `yaml:"log"`
}

const config_file = "./config.yml"

var global_config ServerConfig

func Init() {
	err := readConfig(config_file, &global_config)
	if err != nil {
		fmt.Printf("%v", err.Error())
		os.Exit(-1)
	}
}

func Config() *ServerConfig {
	return &global_config
}

func readConfig(file string, config interface{}) error {
	bytes, err := ioutil.ReadFile(file)
	if err != nil {
		fmt.Printf("open %s failed! err = %s\n", file, err.Error())
		return err
	}

	err = yaml.Unmarshal(bytes, config)
	if err != nil {
		fmt.Printf("yaml Unmarshal %s failed!\n", string(bytes))
		return err
	}
	return nil
}
