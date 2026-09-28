<?php

/**
 * This file is part of OpenMediaVault.
 *
 * @license   https://www.gnu.org/licenses/gpl.html GPL Version 3
 * @author    fffeng99999
 *
 * OpenMediaVault is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * OpenMediaVault is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

namespace Engined\Rpc;

/**
 * FRP client (frpc) RPC service.
 *
 * Settings (conf.service.frpc) and proxy/visitor entries
 * (conf.service.frpc.proxy) live in the OMV configuration database.
 * The Salt state /srv/salt/omv/deploy/frpc renders frpc.toml and, for
 * the native backend, the frpc.service systemd unit. The Docker backend
 * controls an already existing container through /usr/sbin/omv-frpc-ctl.
 *
 * The plugin only manages the OMV-side integration (configuration,
 * lifecycle, status); frpc itself is never modified.
 */
class OMVRpcServiceFrpc extends \OMV\Rpc\ServiceAbstract
{
    const CTL = "/usr/sbin/omv-frpc-ctl";

    /**
     * Settings fields allowed to be persisted by set(). Everything else
     * sent by the form (read-only status fields) is stripped.
     */
    private $settingsFields = [
        "enable", "backend",
        "dockerContainer", "dockerConfigPath", "dockerContainerConfigPath",
        "serverAddr", "serverPort", "user", "token",
        "protocol", "tlsEnable", "tlsServerName", "tcpMuxEnable",
        "poolCount", "heartbeatInterval", "heartbeatTimeout", "dialTimeout",
        "proxyUrl", "dnsServer", "loginFailExit",
        "logLevel", "logMaxDays", "logPrintContent",
        "webEnable", "webAddr", "webPort", "webUser", "webPassword",
        "meta", "includes", "rawConfig"
    ];

    public function getName()
    {
        return "Frpc";
    }

    public function initialize()
    {
        $this->registerMethod("get");
        $this->registerMethod("set");
        $this->registerMethod("getList");
        $this->registerMethod("getProxy");
        $this->registerMethod("setProxy");
        $this->registerMethod("deleteProxy");
        $this->registerMethod("start");
        $this->registerMethod("stop");
        $this->registerMethod("restart");
        $this->registerMethod("reload");
        $this->registerMethod("getLog");
        $this->registerMethod("getShowConfig");
    }

    /**
     * Run omv-frpc-ctl with the given arguments and return the trimmed
     * stdout. Throws on failure when $throwOnError is set.
     */
    private function runCtl(array $args, bool $throwOnError = true): array
    {
        $cmd = new \OMV\System\Process(self::CTL, $args);
        $cmd->setRedirect2to1();
        $cmd->setQuiet(true);
        $output = [];
        $exitStatus = 0;
        $cmd->execute($output, $exitStatus);
        if (0 !== $exitStatus && $throwOnError) {
            throw new \OMV\ExecException($cmd, $output, $exitStatus);
        }
        return [$exitStatus, $output];
    }

    private function getSettingsObject($context): array
    {
        $object = \OMV\Rpc\Rpc::call("Config", "get", [
            "id" => "conf.service.frpc"
        ], $context);
        if (!isset($object["backend"]) || "" === $object["backend"]) {
            $object["backend"] = "docker";
        }
        return $object;
    }

    private function ctlArgs(array $object): array
    {
        return [
            $object["backend"],
            $object["dockerContainer"] ?? "frpc",
            $object["dockerContainerConfigPath"] ?? "/etc/frp/frpc.toml",
            ("native" === $object["backend"])
                ? "/etc/frp/frpc.toml"
                : ($object["dockerConfigPath"] ?? "/etc/frp/frpc.toml")
        ];
    }

    /**
     * Settings object enriched with read-only status information.
     */
    public function get($params, $context)
    {
        $object = $this->getSettingsObject($context);

        // Service state for the active backend. English msgids; the
        // frontend translates them with the 'translate' filter.
        list(, $statusOut) = $this->runCtl(
            array_merge(["is-active"], $this->ctlArgs($object)), false
        );
        $running = (trim(implode("\n", $statusOut)) === "running");
        $object["running"] = $running;
        $object["statusText"] = $running ? "Running" : "Stopped";

        list(, $versionOut) = $this->runCtl(
            array_merge(["version"], $this->ctlArgs($object)), false
        );
        $object["version"] = trim(implode("\n", $versionOut));

        $object["activeConfigPath"] = ("native" === $object["backend"])
            ? "/etc/frp/frpc.toml"
            : ($object["dockerConfigPath"] ?? "");
        return $object;
    }

    public function set($params, $context)
    {
        $data = array_intersect_key($params, array_flip($this->settingsFields));
        return \OMV\Rpc\Rpc::call("Config", "set", [
            "id" => "conf.service.frpc",
            "data" => $data
        ], $context);
    }

    /**
     * Query the frpc admin API (if enabled) for per-proxy live state.
     * Returns an associative array name => status (lowercase).
     */
    private function getProxyStates(array $settings): array
    {
        if (empty($settings["webEnable"])) {
            return [];
        }
        $addr = $settings["webAddr"] ?? "127.0.0.1";
        $port = intval($settings["webPort"] ?? 7400);
        if (empty($addr) || empty($port)) {
            return [];
        }
        $args = ["-fsS", "--max-time", "3"];
        if (!empty($settings["webUser"])) {
            $args[] = "-u";
            $args[] = $settings["webUser"].":".($settings["webPassword"] ?? "");
        }
        $args[] = sprintf("http://%s:%d/api/status", $addr, $port);
        $cmd = new \OMV\System\Process("curl", $args);
        $cmd->setRedirect2to1();
        $cmd->setQuiet(true);
        $output = [];
        $exitStatus = 0;
        $cmd->execute($output, $exitStatus);
        if (0 !== $exitStatus) {
            return [];
        }
        $json = json_decode(implode("\n", $output), true);
        $states = [];
        if (is_array($json) && isset($json["proxies"]) && is_array($json["proxies"])) {
            foreach ($json["proxies"] as $proxy) {
                if (isset($proxy["name"])) {
                    $states[$proxy["name"]] = $proxy;
                }
            }
        }
        return $states;
    }

    public function getList($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $this->validateMethodParams($params, "rpc.common.getlist");

        $db = \OMV\Config\Database::getInstance();
        $objects = $db->get("conf.service.frpc.proxy");
        $settings = $this->getSettingsObject($context);
        $liveStates = $this->getProxyStates($settings);

        $result = [];
        foreach ($objects as $object) {
            $entry = $object->getAssoc();
            $entry["stateText"] = "";
            // Only exposed (enabled) proxies/visitors are reported by
            // the running frpc process; map its live status by name.
            if (!empty($entry["enable"]) && isset($liveStates[$entry["name"] ?? ""])) {
                $info = $liveStates[$entry["name"]];
                $status = strtolower($info["status"] ?? "");
                if ("online" === $status) {
                    $entry["stateText"] = "Online";
                } elseif ("" !== $status) {
                    $entry["stateText"] = ("error" === $status || !empty($info["error"]))
                        ? "Error" : ucfirst($status);
                }
            }
            $result[] = $entry;
        }
        return $this->applyFilter($result,
            $params["start"], $params["limit"],
            $params["sortfield"], $params["sortdir"]);
    }

    public function getProxy($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $this->validateMethodParams($params, "rpc.common.objectuuid");
        $db = \OMV\Config\Database::getInstance();
        return $db->get("conf.service.frpc.proxy", $params["uuid"])->getAssoc();
    }

    public function setProxy($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);

        $object = new \OMV\Config\ConfigObject("conf.service.frpc.proxy");
        $object->setAssoc($params);

        // A proxy/visitor name must be unique; frpc rejects duplicates
        // and the rendered TOML would be ambiguous.
        $db = \OMV\Config\Database::getInstance();
        foreach ($db->get("conf.service.frpc.proxy") as $existing) {
            if ($existing->get("name") === $object->get("name")
                && $existing->get("uuid") !== $object->get("uuid")) {
                throw new \OMV\Exception(
                    "A proxy or visitor with the name '%s' already exists.",
                    $object->get("name")
                );
            }
        }

        $db->set($object);
        return $object->getAssoc();
    }

    public function deleteProxy($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $this->validateMethodParams($params, "rpc.common.objectuuid");
        $db = \OMV\Config\Database::getInstance();
        $object = $db->get("conf.service.frpc.proxy", $params["uuid"]);
        $db->delete($object);
    }

    public function start($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $settings = $this->getSettingsObject($context);
        $this->runCtl(array_merge(["start"], $this->ctlArgs($settings)));
    }

    public function stop($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $settings = $this->getSettingsObject($context);
        $this->runCtl(array_merge(["stop"], $this->ctlArgs($settings)));
    }

    public function restart($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $settings = $this->getSettingsObject($context);
        $this->runCtl(array_merge(["restart"], $this->ctlArgs($settings)));
    }

    public function reload($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $settings = $this->getSettingsObject($context);
        // A failing reload (e.g. admin listener disabled) surfaces the
        // frpc error message to the user; suggest restart as fallback.
        list($exitStatus, ) = $this->runCtl(
            array_merge(["reload"], $this->ctlArgs($settings)), false
        );
        if (0 !== $exitStatus) {
            throw new \OMV\Exception(
                "Hot reload failed. Make sure the frpc admin web server ".
                "(Management section) is enabled, or use Restart instead."
            );
        }
    }

    private function execCtlTask($context, string $op): string
    {
        $settings = $this->getSettingsObject($context);
        $args = array_merge([$op], $this->ctlArgs($settings));
        return $this->execBgProc(
            function ($bgStatusFilename, $bgOutputFilename) use ($args) {
                $cmd = new \OMV\System\Process(self::CTL, $args);
                $cmd->setRedirect2to1();
                if (0 !== ($exitStatus = $this->exec(
                    $cmd, $output, $bgOutputFilename
                ))) {
                    throw new \OMV\ExecException($cmd, $output, $exitStatus);
                }
                return $output;
            }
        );
    }

    public function getLog($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "logs");
    }

    public function getShowConfig($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "showconfig");
    }
}
