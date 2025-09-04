#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <fstream>
#include <mutex>
#include <atomic>
#include "../plc_runtime_core.h"

/**
 * @file ConfigManager.h
 * @brief 配置管理系统头文件
 * 
 * 提供统一的配置管理接口，支持多种配置源和格式
 */

namespace Uranus {

/**
 * @brief 配置值类型枚举
 */
enum class ConfigValueType {
    STRING,
    INTEGER,
    DOUBLE,
    BOOLEAN,
    ARRAY,
    OBJECT
};

/**
 * @brief 配置值包装类
 */
class ConfigValue {
public:
    ConfigValue() : type_(ConfigValueType::STRING), string_value_("") {}
    explicit ConfigValue(const std::string& value) : type_(ConfigValueType::STRING), string_value_(value) {}
    explicit ConfigValue(int value) : type_(ConfigValueType::INTEGER), int_value_(value) {}
    explicit ConfigValue(double value) : type_(ConfigValueType::DOUBLE), double_value_(value) {}
    explicit ConfigValue(bool value) : type_(ConfigValueType::BOOLEAN), bool_value_(value) {}
    
    ConfigValueType getType() const { return type_; }
    
    std::string asString() const;
    int asInt() const;
    double asDouble() const;
    bool asBool() const;
    
    bool isValid() const { return type_ != ConfigValueType::STRING || !string_value_.empty(); }
    
private:
    ConfigValueType type_;
    std::string string_value_;
    int int_value_ = 0;
    double double_value_ = 0.0;
    bool bool_value_ = false;
};

/**
 * @brief 配置源接口
 */
class IConfigSource {
public:
    virtual ~IConfigSource() = default;
    
    /**
     * @brief 加载配置
     * @param config 配置映射
     * @return 是否成功
     */
    virtual bool load(std::unordered_map<std::string, ConfigValue>& config) = 0;
    
    /**
     * @brief 保存配置
     * @param config 配置映射
     * @return 是否成功
     */
    virtual bool save(const std::unordered_map<std::string, ConfigValue>& config) = 0;
    
    /**
     * @brief 获取配置源名称
     */
    virtual std::string getName() const = 0;
};

/**
 * @brief JSON文件配置源
 */
class JsonFileConfigSource : public IConfigSource {
public:
    explicit JsonFileConfigSource(const std::string& filePath);
    
    bool load(std::unordered_map<std::string, ConfigValue>& config) override;
    bool save(const std::unordered_map<std::string, ConfigValue>& config) override;
    std::string getName() const override { return "JsonFile: " + filePath_; }
    
private:
    std::string filePath_;
    bool parseJsonValue(const std::string& jsonStr, std::unordered_map<std::string, ConfigValue>& config);
    std::string generateJsonString(const std::unordered_map<std::string, ConfigValue>& config);
};

/**
 * @brief 环境变量配置源
 */
class EnvironmentConfigSource : public IConfigSource {
public:
    explicit EnvironmentConfigSource(const std::string& prefix = "PLC_");
    
    bool load(std::unordered_map<std::string, ConfigValue>& config) override;
    bool save(const std::unordered_map<std::string, ConfigValue>& config) override;
    std::string getName() const override { return "Environment: " + prefix_; }
    
private:
    std::string prefix_;
};

/**
 * @brief 配置验证器接口
 */
class IConfigValidator {
public:
    virtual ~IConfigValidator() = default;
    
    /**
     * @brief 验证配置值
     * @param key 配置键
     * @param value 配置值
     * @param errorMsg 错误信息输出
     * @return 是否有效
     */
    virtual bool validate(const std::string& key, const ConfigValue& value, std::string& errorMsg) = 0;
};

/**
 * @brief 范围验证器
 */
class RangeValidator : public IConfigValidator {
public:
    RangeValidator(double minValue, double maxValue) : minValue_(minValue), maxValue_(maxValue) {}
    
    bool validate(const std::string& key, const ConfigValue& value, std::string& errorMsg) override;
    
private:
    double minValue_;
    double maxValue_;
};

/**
 * @brief 配置管理器主类
 */
class ConfigManager {
public:
    static ConfigManager& getInstance();
    
    // 禁用拷贝和移动
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;
    
    /**
     * @brief 初始化配置管理器
     * @param defaultConfigPath 默认配置文件路径
     * @return 错误代码
     */
    ErrorCode initialize(const std::string& defaultConfigPath = "config.json");
    
    /**
     * @brief 添加配置源
     * @param source 配置源
     * @param priority 优先级（数字越大优先级越高）
     */
    void addConfigSource(std::unique_ptr<IConfigSource> source, int priority = 0);
    
    /**
     * @brief 添加配置验证器
     * @param key 配置键
     * @param validator 验证器
     */
    void addValidator(const std::string& key, std::unique_ptr<IConfigValidator> validator);
    
    /**
     * @brief 加载所有配置
     * @return 错误代码
     */
    ErrorCode loadConfig();
    
    /**
     * @brief 保存配置到主配置源
     * @return 错误代码
     */
    ErrorCode saveConfig();
    
    /**
     * @brief 获取配置值
     * @param key 配置键
     * @param defaultValue 默认值
     * @return 配置值
     */
    template<typename T>
    T getConfig(const std::string& key, const T& defaultValue) const;
    
    /**
     * @brief 设置配置值
     * @param key 配置键
     * @param value 配置值
     * @return 是否成功
     */
    template<typename T>
    bool setConfig(const std::string& key, const T& value);
    
    /**
     * @brief 检查配置键是否存在
     * @param key 配置键
     * @return 是否存在
     */
    bool hasConfig(const std::string& key) const;
    
    /**
     * @brief 从SystemConfig结构加载配置
     * @param sysConfig 系统配置结构
     */
    void loadFromSystemConfig(const SystemConfig& sysConfig);
    
    /**
     * @brief 转换为SystemConfig结构
     * @return 系统配置结构
     */
    SystemConfig toSystemConfig() const;
    
    /**
     * @brief 注册配置变更回调
     * @param key 配置键
     * @param callback 回调函数
     */
    void registerChangeCallback(const std::string& key, std::function<void(const ConfigValue&)> callback);
    
    /**
     * @brief 获取所有配置键
     * @return 配置键列表
     */
    std::vector<std::string> getAllKeys() const;
    
    /**
     * @brief 验证所有配置
     * @param errors 错误信息输出
     * @return 是否全部有效
     */
    bool validateAll(std::vector<std::string>& errors) const;
    
    /**
     * @brief 重置为默认配置
     */
    void resetToDefaults();
    
    /**
     * @brief 获取配置统计信息
     */
    struct ConfigStats {
        size_t totalConfigs = 0;
        size_t validConfigs = 0;
        size_t sourcesCount = 0;
        size_t validatorsCount = 0;
    };
    
    ConfigStats getStats() const;
    
private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    
    void setupDefaultValidators();
    void notifyConfigChange(const std::string& key, const ConfigValue& value);
    
    mutable std::mutex configMutex_;
    std::unordered_map<std::string, ConfigValue> config_;
    
    // 配置源管理（按优先级排序）
    std::vector<std::pair<std::unique_ptr<IConfigSource>, int>> configSources_;
    
    // 验证器管理
    std::unordered_map<std::string, std::unique_ptr<IConfigValidator>> validators_;
    
    // 变更回调
    std::unordered_map<std::string, std::vector<std::function<void(const ConfigValue&)>>> changeCallbacks_;
    
    std::atomic<bool> initialized_{false};
};

// 模板实现
template<typename T>
T ConfigManager::getConfig(const std::string& key, const T& defaultValue) const {
    std::lock_guard<std::mutex> lock(configMutex_);
    
    auto it = config_.find(key);
    if (it == config_.end()) {
        return defaultValue;
    }
    
    const ConfigValue& value = it->second;
    
    if constexpr (std::is_same_v<T, std::string>) {
        return value.asString();
    } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, uint32_t>) {
        return static_cast<T>(value.asInt());
    } else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
        return static_cast<T>(value.asDouble());
    } else if constexpr (std::is_same_v<T, bool>) {
        return value.asBool();
    } else {
        return defaultValue;
    }
}

template<typename T>
bool ConfigManager::setConfig(const std::string& key, const T& value) {
    ConfigValue configValue;
    
    if constexpr (std::is_same_v<T, std::string>) {
        configValue = ConfigValue(value);
    } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, uint32_t>) {
        configValue = ConfigValue(static_cast<int>(value));
    } else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
        configValue = ConfigValue(static_cast<double>(value));
    } else if constexpr (std::is_same_v<T, bool>) {
        configValue = ConfigValue(value);
    } else {
        return false;
    }
    
    // 验证配置值
    std::string errorMsg;
    auto validatorIt = validators_.find(key);
    if (validatorIt != validators_.end()) {
        if (!validatorIt->second->validate(key, configValue, errorMsg)) {
            return false;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        config_[key] = configValue;
    }
    
    notifyConfigChange(key, configValue);
    return true;
}

} // namespace Uranus