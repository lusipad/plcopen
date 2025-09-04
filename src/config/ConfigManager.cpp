#include "../../include/config/ConfigManager.h"
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <regex>

namespace Uranus {

// ConfigValue 实现
std::string ConfigValue::asString() const {
    switch (type_) {
        case ConfigValueType::STRING:
            return string_value_;
        case ConfigValueType::INTEGER:
            return std::to_string(int_value_);
        case ConfigValueType::DOUBLE:
            return std::to_string(double_value_);
        case ConfigValueType::BOOLEAN:
            return bool_value_ ? "true" : "false";
        default:
            return "";
    }
}

int ConfigValue::asInt() const {
    switch (type_) {
        case ConfigValueType::INTEGER:
            return int_value_;
        case ConfigValueType::DOUBLE:
            return static_cast<int>(double_value_);
        case ConfigValueType::BOOLEAN:
            return bool_value_ ? 1 : 0;
        case ConfigValueType::STRING:
            try {
                return std::stoi(string_value_);
            } catch (...) {
                return 0;
            }
        default:
            return 0;
    }
}

double ConfigValue::asDouble() const {
    switch (type_) {
        case ConfigValueType::DOUBLE:
            return double_value_;
        case ConfigValueType::INTEGER:
            return static_cast<double>(int_value_);
        case ConfigValueType::BOOLEAN:
            return bool_value_ ? 1.0 : 0.0;
        case ConfigValueType::STRING:
            try {
                return std::stod(string_value_);
            } catch (...) {
                return 0.0;
            }
        default:
            return 0.0;
    }
}

bool ConfigValue::asBool() const {
    switch (type_) {
        case ConfigValueType::BOOLEAN:
            return bool_value_;
        case ConfigValueType::INTEGER:
            return int_value_ != 0;
        case ConfigValueType::DOUBLE:
            return double_value_ != 0.0;
        case ConfigValueType::STRING:
            return string_value_ == "true" || string_value_ == "1" || string_value_ == "yes";
        default:
            return false;
    }
}

// JsonFileConfigSource 实现
JsonFileConfigSource::JsonFileConfigSource(const std::string& filePath) 
    : filePath_(filePath) {
}

bool JsonFileConfigSource::load(std::unordered_map<std::string, ConfigValue>& config) {
    std::ifstream file(filePath_);
    if (!file.is_open()) {
        return false;
    }
    
    std::string jsonContent((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    file.close();
    
    return parseJsonValue(jsonContent, config);
}

bool JsonFileConfigSource::save(const std::unordered_map<std::string, ConfigValue>& config) {
    std::ofstream file(filePath_);
    if (!file.is_open()) {
        return false;
    }
    
    std::string jsonContent = generateJsonString(config);
    file << jsonContent;
    file.close();
    
    return true;
}

bool JsonFileConfigSource::parseJsonValue(const std::string& jsonStr, 
                                        std::unordered_map<std::string, ConfigValue>& config) {
    // 简化的JSON解析器（仅支持基本键值对）
    std::regex keyValueRegex(R"("([^"]+)"\s*:\s*([^,}]+))");
    std::sregex_iterator iter(jsonStr.begin(), jsonStr.end(), keyValueRegex);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        std::string key = (*iter)[1].str();
        std::string valueStr = (*iter)[2].str();
        
        // 去除空白字符
        valueStr.erase(0, valueStr.find_first_not_of(" \t\n\r"));
        valueStr.erase(valueStr.find_last_not_of(" \t\n\r") + 1);
        
        ConfigValue value;
        
        // 判断值类型
        if (valueStr.front() == '"' && valueStr.back() == '"') {
            // 字符串值
            value = ConfigValue(valueStr.substr(1, valueStr.length() - 2));
        } else if (valueStr == "true" || valueStr == "false") {
            // 布尔值
            value = ConfigValue(valueStr == "true");
        } else if (valueStr.find('.') != std::string::npos) {
            // 浮点数
            try {
                value = ConfigValue(std::stod(valueStr));
            } catch (...) {
                continue;
            }
        } else {
            // 整数
            try {
                value = ConfigValue(std::stoi(valueStr));
            } catch (...) {
                continue;
            }
        }
        
        config[key] = value;
    }
    
    return true;
}

std::string JsonFileConfigSource::generateJsonString(const std::unordered_map<std::string, ConfigValue>& config) {
    std::ostringstream oss;
    oss << "{\n";
    
    bool first = true;
    for (const auto& [key, value] : config) {
        if (!first) {
            oss << ",\n";
        }
        first = false;
        
        oss << "  \"" << key << "\": ";
        
        switch (value.getType()) {
            case ConfigValueType::STRING:
                oss << "\"" << value.asString() << "\"";
                break;
            case ConfigValueType::BOOLEAN:
                oss << (value.asBool() ? "true" : "false");
                break;
            case ConfigValueType::INTEGER:
                oss << value.asInt();
                break;
            case ConfigValueType::DOUBLE:
                oss << value.asDouble();
                break;
            default:
                oss << "\"" << value.asString() << "\"";
                break;
        }
    }
    
    oss << "\n}";
    return oss.str();
}

// EnvironmentConfigSource 实现
EnvironmentConfigSource::EnvironmentConfigSource(const std::string& prefix) 
    : prefix_(prefix) {
}

bool EnvironmentConfigSource::load(std::unordered_map<std::string, ConfigValue>& config) {
    // 在Windows上获取环境变量
    char* env = nullptr;
    size_t len = 0;
    
    // 获取所有环境变量（简化实现）
    const char* commonEnvVars[] = {
        "PLC_SCHEDULER_CYCLE_PERIOD_US",
        "PLC_MAX_TASKS",
        "PLC_TOTAL_MEMORY_BUDGET",
        "PLC_REALTIME_MEMORY_BUDGET",
        "PLC_MAX_DIGITAL_IO",
        "PLC_MAX_ANALOG_IO",
        "PLC_IO_SCAN_PERIOD_US",
        "PLC_MAX_CONNECTIONS",
        "PLC_COMMUNICATION_TIMEOUT_MS",
        "PLC_MAX_AXES",
        "PLC_POSITION_ACCURACY",
        "PLC_VELOCITY_ACCURACY"
    };
    
    for (const char* envVar : commonEnvVars) {
        if (_dupenv_s(&env, &len, envVar) == 0 && env != nullptr) {
            std::string key = envVar;
            // 移除前缀
            if (key.substr(0, prefix_.length()) == prefix_) {
                key = key.substr(prefix_.length());
            }
            
            std::string value = env;
            
            // 尝试解析为不同类型
            if (value == "true" || value == "false") {
                config[key] = ConfigValue(value == "true");
            } else if (value.find('.') != std::string::npos) {
                try {
                    config[key] = ConfigValue(std::stod(value));
                } catch (...) {
                    config[key] = ConfigValue(value);
                }
            } else {
                try {
                    config[key] = ConfigValue(std::stoi(value));
                } catch (...) {
                    config[key] = ConfigValue(value);
                }
            }
            
            free(env);
        }
    }
    
    return true;
}

bool EnvironmentConfigSource::save(const std::unordered_map<std::string, ConfigValue>& config) {
    // 环境变量配置源通常是只读的
    return false;
}

// RangeValidator 实现
bool RangeValidator::validate(const std::string& key, const ConfigValue& value, std::string& errorMsg) {
    double val = value.asDouble();
    if (val < minValue_ || val > maxValue_) {
        std::ostringstream oss;
        oss << "Configuration '" << key << "' value " << val 
            << " is out of range [" << minValue_ << ", " << maxValue_ << "]";
        errorMsg = oss.str();
        return false;
    }
    return true;
}

// ConfigManager 实现
ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

ErrorCode ConfigManager::initialize(const std::string& defaultConfigPath) {
    if (initialized_.load()) {
        return ErrorCode::SUCCESS;
    }
    
    try {
        // 添加默认配置源
        addConfigSource(std::make_unique<JsonFileConfigSource>(defaultConfigPath), 100);
        addConfigSource(std::make_unique<EnvironmentConfigSource>("PLC_"), 200);
        
        // 设置默认验证器
        setupDefaultValidators();
        
        // 加载配置
        ErrorCode result = loadConfig();
        if (result != ErrorCode::SUCCESS) {
            return result;
        }
        
        initialized_.store(true);
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return ErrorCode::SYSTEM_CONFIG_INVALID;
    }
}

void ConfigManager::addConfigSource(std::unique_ptr<IConfigSource> source, int priority) {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    configSources_.emplace_back(std::move(source), priority);
    
    // 按优先级排序（高优先级在前）
    std::sort(configSources_.begin(), configSources_.end(),
              [](const auto& a, const auto& b) {
                  return a.second > b.second;
              });
}

void ConfigManager::addValidator(const std::string& key, std::unique_ptr<IConfigValidator> validator) {
    std::lock_guard<std::mutex> lock(configMutex_);
    validators_[key] = std::move(validator);
}

ErrorCode ConfigManager::loadConfig() {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    // 从低优先级到高优先级加载配置（高优先级覆盖低优先级）
    for (auto it = configSources_.rbegin(); it != configSources_.rend(); ++it) {
        std::unordered_map<std::string, ConfigValue> sourceConfig;
        if (it->first->load(sourceConfig)) {
            // 合并配置
            for (const auto& [key, value] : sourceConfig) {
                config_[key] = value;
            }
        }
    }
    
    return ErrorCode::SUCCESS;
}

ErrorCode ConfigManager::saveConfig() {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    // 保存到最高优先级的可写配置源
    for (const auto& [source, priority] : configSources_) {
        if (source->save(config_)) {
            return ErrorCode::SUCCESS;
        }
    }
    
    return ErrorCode::SYSTEM_CONFIG_INVALID;
}

bool ConfigManager::hasConfig(const std::string& key) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return config_.find(key) != config_.end();
}

void ConfigManager::loadFromSystemConfig(const SystemConfig& sysConfig) {
    setConfig("schedulerCyclePeriodUs", sysConfig.schedulerCyclePeriodUs);
    setConfig("maxTasks", sysConfig.maxTasks);
    setConfig("totalMemoryBudget", static_cast<double>(sysConfig.totalMemoryBudget));
    setConfig("realtimeMemoryBudget", static_cast<double>(sysConfig.realtimeMemoryBudget));
    setConfig("maxDigitalIO", sysConfig.maxDigitalIO);
    setConfig("maxAnalogIO", sysConfig.maxAnalogIO);
    setConfig("ioScanPeriodUs", sysConfig.ioScanPeriodUs);
    setConfig("maxConnections", sysConfig.maxConnections);
    setConfig("communicationTimeoutMs", sysConfig.communicationTimeoutMs);
    setConfig("maxAxes", sysConfig.maxAxes);
    setConfig("positionAccuracy", sysConfig.positionAccuracy);
    setConfig("velocityAccuracy", sysConfig.velocityAccuracy);
}

SystemConfig ConfigManager::toSystemConfig() const {
    SystemConfig config;
    
    config.schedulerCyclePeriodUs = getConfig<uint32_t>("schedulerCyclePeriodUs", 1000);
    config.maxTasks = getConfig<uint32_t>("maxTasks", 256);
    config.totalMemoryBudget = static_cast<size_t>(getConfig<double>("totalMemoryBudget", 64 * 1024 * 1024));
    config.realtimeMemoryBudget = static_cast<size_t>(getConfig<double>("realtimeMemoryBudget", 16 * 1024 * 1024));
    config.maxDigitalIO = getConfig<uint32_t>("maxDigitalIO", 4096);
    config.maxAnalogIO = getConfig<uint32_t>("maxAnalogIO", 1024);
    config.ioScanPeriodUs = getConfig<uint32_t>("ioScanPeriodUs", 100);
    config.maxConnections = getConfig<uint32_t>("maxConnections", 64);
    config.communicationTimeoutMs = getConfig<uint32_t>("communicationTimeoutMs", 5000);
    config.maxAxes = getConfig<uint32_t>("maxAxes", 32);
    config.positionAccuracy = getConfig<double>("positionAccuracy", 1.0);
    config.velocityAccuracy = getConfig<double>("velocityAccuracy", 0.1);
    
    return config;
}

void ConfigManager::registerChangeCallback(const std::string& key, 
                                         std::function<void(const ConfigValue&)> callback) {
    std::lock_guard<std::mutex> lock(configMutex_);
    changeCallbacks_[key].push_back(callback);
}

std::vector<std::string> ConfigManager::getAllKeys() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    std::vector<std::string> keys;
    keys.reserve(config_.size());
    
    for (const auto& [key, value] : config_) {
        keys.push_back(key);
    }
    
    return keys;
}

bool ConfigManager::validateAll(std::vector<std::string>& errors) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    bool allValid = true;
    errors.clear();
    
    for (const auto& [key, value] : config_) {
        auto validatorIt = validators_.find(key);
        if (validatorIt != validators_.end()) {
            std::string errorMsg;
            if (!validatorIt->second->validate(key, value, errorMsg)) {
                errors.push_back(errorMsg);
                allValid = false;
            }
        }
    }
    
    return allValid;
}

void ConfigManager::resetToDefaults() {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    config_.clear();
    
    // 加载默认的SystemConfig
    SystemConfig defaultConfig;
    loadFromSystemConfig(defaultConfig);
}

ConfigManager::ConfigStats ConfigManager::getStats() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    ConfigStats stats;
    stats.totalConfigs = config_.size();
    stats.sourcesCount = configSources_.size();
    stats.validatorsCount = validators_.size();
    
    // 计算有效配置数量
    for (const auto& [key, value] : config_) {
        if (value.isValid()) {
            stats.validConfigs++;
        }
    }
    
    return stats;
}

void ConfigManager::setupDefaultValidators() {
    // 调度器配置验证
    addValidator("schedulerCyclePeriodUs", std::make_unique<RangeValidator>(100, 100000));
    addValidator("maxTasks", std::make_unique<RangeValidator>(1, 10000));
    
    // 内存配置验证
    addValidator("totalMemoryBudget", std::make_unique<RangeValidator>(1024 * 1024, 1024LL * 1024 * 1024 * 1024));
    addValidator("realtimeMemoryBudget", std::make_unique<RangeValidator>(1024 * 1024, 512LL * 1024 * 1024));
    
    // I/O配置验证
    addValidator("maxDigitalIO", std::make_unique<RangeValidator>(1, 65536));
    addValidator("maxAnalogIO", std::make_unique<RangeValidator>(1, 16384));
    addValidator("ioScanPeriodUs", std::make_unique<RangeValidator>(10, 10000));
    
    // 通信配置验证
    addValidator("maxConnections", std::make_unique<RangeValidator>(1, 1000));
    addValidator("communicationTimeoutMs", std::make_unique<RangeValidator>(100, 60000));
    
    // 运动控制配置验证
    addValidator("maxAxes", std::make_unique<RangeValidator>(1, 256));
    addValidator("positionAccuracy", std::make_unique<RangeValidator>(0.001, 1000.0));
    addValidator("velocityAccuracy", std::make_unique<RangeValidator>(0.001, 100.0));
}

void ConfigManager::notifyConfigChange(const std::string& key, const ConfigValue& value) {
    auto callbackIt = changeCallbacks_.find(key);
    if (callbackIt != changeCallbacks_.end()) {
        for (const auto& callback : callbackIt->second) {
            try {
                callback(value);
            } catch (const std::exception& e) {
                // 记录回调错误，但不影响配置设置
                std::cerr << "Config change callback error for key '" << key << "': " << e.what() << std::endl;
            }
        }
    }
}

} // namespace Uranus