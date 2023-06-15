package httphandler

import (
	"fmt"
	"net/http"
	"os"

	"gitee.com/openeuler/PilotGo-plugins/sdk/common"
	"gitee.com/openeuler/PilotGo-plugins/sdk/plugin/client"
	"github.com/gin-gonic/gin"
	"openeuler.org/PilotGo/gala-ops-plugin/plugin"
)

func InstallGopher(ctx *gin.Context) {
	param := &common.Batch{}
	if err := ctx.BindJSON(param); err != nil {
		ctx.JSON(http.StatusBadRequest, gin.H{
			"code":   -1,
			"status": "parameter error",
		})
	}

	script, err := os.ReadFile("../sh-script/deploy_gopher")
	if err != nil {
		ctx.JSON(http.StatusBadRequest, gin.H{
			"code":   -1,
			"status": fmt.Sprintf("read deploy script error:%s", err),
		})
	}

	cmdResults, err := Galaops.Sdkmethod.RunScript(param, string(script))
	if err != nil {
		ctx.JSON(http.StatusBadRequest, gin.H{
			"code":   -1,
			"status": fmt.Sprintf("run remote script error:%s", err),
		})
	}

	ret := []interface{}{}
	monitorTargets := []string{}
	for _, result := range cmdResults {
		d := struct {
			MachineUUID   string
			MachineIP     string
			InstallStatus string
			Error         string
		}{
			MachineUUID:   result.MachineUUID,
			InstallStatus: "ok",
			Error:         "",
		}

		if result.RetCode != 0 {
			d.InstallStatus = "error"
			d.Error = result.Stderr
		} else {
			// TODO: add gala-gopher to prometheus monitor target here
			// default exporter port :8888
			monitorTargets = append(monitorTargets, result.MachineIP+":8888")
		}

		ret = append(ret, d)
	}
	err = plugin.MonitorTargets(monitorTargets, Galaops.PromePlugin["url"].(string))
	if err != nil {
		fmt.Println("error: failed to add gala-gopher to prometheus monitor targets")
	}

	ctx.JSON(http.StatusOK, gin.H{
		"code":   0,
		"status": "ok",
		"data":   ret,
	})
}

func UpgradeGopher(ctx *gin.Context) {
	// TODO
	param := &common.Batch{}
	if err := ctx.BindJSON(param); err != nil {
		ctx.JSON(http.StatusBadRequest, gin.H{
			"code":   -1,
			"status": "parameter error",
		})
	}

	cmd := "systemctl stop gala-gopher && yum upgrade -y gala-gopher && systemctl start gala-gopher"
	cmdResults, err := client.GetClient().RunCommand(param, cmd)
	if err != nil {
		ctx.JSON(http.StatusBadRequest, gin.H{
			"code":   -1,
			"status": fmt.Sprintf("run remote script error:%s", err),
		})
	}

	ret := []interface{}{}
	for _, result := range cmdResults {
		d := struct {
			MachineUUID     string
			UninstallStatus string
			Error           string
		}{
			MachineUUID:     result.MachineUUID,
			UninstallStatus: "ok",
			Error:           "",
		}

		if result.RetCode != 0 {
			d.UninstallStatus = "error"
			d.Error = result.Stderr
		}

		ret = append(ret, d)
	}

	ctx.JSON(http.StatusOK, gin.H{
		"code":   0,
		"status": "ok",
		"data":   ret,
	})
}

func UninstallGopher(ctx *gin.Context) {
	// TODO
	param := &common.Batch{}
	if err := ctx.BindJSON(param); err != nil {
		ctx.JSON(http.StatusBadRequest, gin.H{
			"code":   -1,
			"status": "parameter error",
		})
	}

	cmd := "systemctl stop gala-gopher && yum autoremove -y gala-gopher"
	cmdResults, err := client.GetClient().RunCommand(param, cmd)
	if err != nil {
		ctx.JSON(http.StatusBadRequest, gin.H{
			"code":   -1,
			"status": fmt.Sprintf("run remote script error:%s", err),
		})
	}

	ret := []interface{}{}
	for _, result := range cmdResults {
		d := struct {
			MachineUUID     string
			UninstallStatus string
			Error           string
		}{
			MachineUUID:     result.MachineUUID,
			UninstallStatus: "ok",
			Error:           "",
		}

		if result.RetCode != 0 {
			d.UninstallStatus = "error"
			d.Error = result.Stderr
		}

		ret = append(ret, d)
	}

	ctx.JSON(http.StatusOK, gin.H{
		"code":   0,
		"status": "ok",
		"data":   ret,
	})
}
