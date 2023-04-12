#include "foundation/JsonSerializer.h"
#include "refl.hpp"
#include <spdlog/spdlog.h>

#include <set>
#include <map>

enum class FuelType {
	Petrol,
	Diesel,
	Electric
};

using FeatureId = std::string;

struct Car {
	std::map<f64, std::string> features;
	FuelType fuelType = FuelType::Petrol;
	f32 price;
	std::string make;
	std::string model;
	int year;
	std::vector<f32> tyrePressure;
};

REFL_AUTO(
	type(Car),
	field(features),
	field(fuelType),
	field(price),
	field(make),
	field(model),
	field(year),
	field(tyrePressure)
)

void jsontest() {	
	std::vector<std::string> vs;
	auto it = vs.insert(vs.end(), "hiiii");
	//std::string& re = *it;

	simdjson::simdjson_result<simdjson::ondemand::object> obj;
	simdjson::simdjson_result<simdjson::ondemand::raw_json_string> rj;

	//std::string_view kv = rj;

	for (auto elem : obj) {
		//elem.key().value().
	}

	typename std::map<std::string, std::string>::key_type key = "key";
	typename std::map<std::string, std::string>::value_type value = std::make_pair("key", "value");
	std::map<std::string, std::string> m;
	

	simdjson::ondemand::parser carParser;
	simdjson::ondemand::parser carsParser;
	simdjson::ondemand::parser carsParser2;

	auto cars_json = R"( [
  { "features": { "1337": "bar" }, "fuelType": "Diesel", "price": 10.2, "make": "Toyota", "model": "Camry",  "year": 2018, "tyrePressure": [ 40.1, 39.9, 37.7, 40.4 ] },
  { "features": {}, "fuelType": 0, "price": 15.0, "make": "Kia",    "model": "Soul",   "year": 2012, "tyrePressure": [ 30.1, 31.0, 28.6, 28.7 ] },
  { "features": {}, "fuelType": "Electric", "price": 17.9, "make": "Toyota", "model": "Tercel", "year": 1999, "tyrePressure": [ 29.8, 30.0, 30.2, 30.5 ] }
] )"_padded;

	auto car_json = R"({ "features": { "1337": "bar" }, "fuelType": "Diesel", "price": 10.2, "make": "Toyota", "model": "Camry", "year": 2018, "tyrePressure": [ 40.1, 39.9, 37.7, 40.4 ] })"_padded;

	simdjson::ondemand::document carDoc = carParser.iterate(car_json);
	simdjson::ondemand::document carsDoc = carsParser.iterate(cars_json);
	simdjson::ondemand::document carsDoc2 = carsParser2.iterate(cars_json);

	Car car;
	std::array<Car, 3> cars;	
	std::vector<Car> cars2;

	fw::JsonSerializer::deserializeFromString(car_json, car);
	fw::JsonSerializer::deserialize(carsDoc, cars);
	fw::JsonSerializer::deserialize(carsDoc2, cars2);

	spdlog::info("");
}