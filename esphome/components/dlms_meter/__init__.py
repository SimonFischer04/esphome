import esphome.config_validation as cv

CONF_MY_DECRYPTION_KEY = "decryption_key"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_MY_DECRYPTION_KEY): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)
