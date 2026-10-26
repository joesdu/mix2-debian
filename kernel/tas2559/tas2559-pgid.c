// SPDX-License-Identifier: GPL-2.0-only
/* Chiron identity diagnostic: reset pulse, ID reads, then hold in reset. */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define TAS2559_PGID 0x03
#define TAS2560_ID 0x7d

static const struct regmap_config tas_pgid_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
	.cache_type = REGCACHE_NONE,
};

static void tas_pgid_hold_reset(void *data)
{
	/* The test DT uses GPIO_ACTIVE_HIGH; raw zero asserts physical RESET_N. */
	gpiod_set_raw_value_cansleep(data, 0);
}

static int tas_pgid_probe(struct i2c_client *client)
{
	struct gpio_desc *reset;
	struct regmap *map;
	struct i2c_client *companion;
	struct regmap *companion_map;
	unsigned int value;
	int ret;

	reset = devm_gpiod_get(&client->dev, "reset", GPIOD_ASIS);
	if (IS_ERR(reset))
		return PTR_ERR(reset);
	ret = gpiod_direction_output_raw(reset, 0);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(&client->dev, tas_pgid_hold_reset, reset);
	if (ret)
		return ret;
	msleep(5);
	gpiod_set_raw_value_cansleep(reset, 1);
	msleep(2);

	map = devm_regmap_init_i2c(client, &tas_pgid_regmap);
	if (IS_ERR(map))
		return PTR_ERR(map);

	ret = regmap_read(map, TAS2559_PGID, &value);
	if (ret)
		return dev_err_probe(&client->dev, ret, "TAS2559 PGID read failed\n");
	dev_info(&client->dev, "TAS2559 PGID=0x%02x\n", value);

	companion = devm_i2c_new_dummy_device(&client->dev, client->adapter, 0x4d);
	if (IS_ERR(companion))
		return PTR_ERR(companion);
	companion_map = devm_regmap_init_i2c(companion, &tas_pgid_regmap);
	if (IS_ERR(companion_map))
		return PTR_ERR(companion_map);

	ret = regmap_read(companion_map, TAS2560_ID, &value);
	if (ret)
		return dev_err_probe(&client->dev, ret, "TAS2560 ID read failed\n");
	dev_info(&client->dev, "TAS2560 ID=0x%02x\n", value);
	tas_pgid_hold_reset(reset);
	dev_info(&client->dev, "identity test complete; RESET_N held low\n");

	return 0;
}

static const struct of_device_id tas_pgid_of_match[] = {
	{ .compatible = "ti,tas2559" },
	{ }
};
MODULE_DEVICE_TABLE(of, tas_pgid_of_match);

static struct i2c_driver tas_pgid_driver = {
	.driver = {
		.name = "tas2559-pgid-readonly",
		.of_match_table = tas_pgid_of_match,
	},
	.probe = tas_pgid_probe,
};
module_i2c_driver(tas_pgid_driver);

MODULE_DESCRIPTION("Read-only TAS2559/TAS2560 PGID diagnostic");
MODULE_LICENSE("GPL");
