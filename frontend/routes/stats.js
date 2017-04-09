var express = require('express');
var router = express.Router();
const Influx = require('influx');

const influx = new Influx.InfluxDB({
 host: process.env.INFLUX_HOST,
 database: 'rtt',
 schema: [
   {
     measurement: 'latency',
     fields: {
       label: Influx.FieldType.STRING,
       total: Influx.FieldType.INTEGER
     },
     tags: [
      'destination_as',
      'destination_asn',
      'destination_city',
      'destination_country',
      'destination_countrycode',
      'destination_lat',
      'destination_long',
      'source_as',
      'source_asn',
      'source_city',
      'source_country',
      'source_countrycode',
      'source_lat',
      'source_long',
     ]
   }
 ]
})

var stats_destination_countries = {};
var stats_destination_cities = {};
var stats_source_cities = {};
var stats_destination_asn = {};
var stats_source_countries = {};
var stats_source_asn = {};

// This here repeats and queries stats every 20 second
setInterval(function(){

  influx.query("select min(total), mean(total) as mean, max(total) \
                from latency \
                where destination_country != '-' \
                AND time >=  now() - 10m \
                group by destination_country, destination_countrycode")
    .then(results => {
      stats_destination_countries = {};
      results.forEach(function(row) {
        stats_destination_countries[row['destination_country']] = row;
        stats_destination_countries[row['destination_country']]['countrycode'] = row['destination_countrycode'];
      });
    });

  influx.query("select min(total), mean(total) as mean, max(total) \
                from latency \
                where source_country != '-' \
                AND time >=  now() - 10m \
                group by source_country, source_countrycode")
    .then(results => {
      stats_source_countries = {};
      results.forEach(function(row) {
        stats_source_countries[row['source_country']] = row;
        stats_source_countries[row['source_country']]['countrycode'] = row['source_countrycode'];
      });
    });

  influx.query("select min(total), mean(total) as mean, max(total) \
                from latency \
                where source_city != '-' \
                AND time >=  now() - 10m \
                group by source_city, source_countrycode")
    .then(results => {
      stats_source_cities = {};
      results.forEach(function(row) {
        stats_source_cities[row['source_city']] = row;
        stats_source_cities[row['source_city']]['countrycode'] = row['source_countrycode'];
      }); 
    });

  influx.query("select min(total), mean(total) as mean, max(total) \
              from latency \
              where destination_city != '-' \
              AND time >=  now() - 10m \
              group by destination_city, destination_countrycode")
  .then(results => {
    stats_destination_cities = {};
    results.forEach(function(row) {
      stats_destination_cities[row['destination_city']] = row;
      stats_destination_cities[row['destination_city']]['countrycode'] = row['destination_countrycode'];
    }); 
  });

  influx.query("select min(total), mean(total) as mean, max(total) \
              from latency \
              where destination_as != '-' \
              AND time >=  now() - 10m \
              group by destination_asn, destination_as")
  .then(results => {
    stats_destination_asn = {};
    results.forEach(function(row) {
      stats_destination_asn[row['destination_asn']] = row;
      stats_destination_asn[row['destination_asn']]['name'] = row['destination_as'];
    }); 
  });

  influx.query("select min(total), mean(total) as mean, max(total) \
              from latency \
              where source_as != '-' \
              AND time >=  now() - 10m \
              group by source_asn, source_as")
  .then(results => {
    stats_source_asn = {};
    results.forEach(function(row) {
      stats_source_asn[row['source_asn']] = row;
      stats_source_asn[row['source_asn']]['name'] = row['source_as'];
    }); 
  });

  console.log(stats_destination_countries);

}, 20000); 


function getTop5Stats () {

  var top5_source = Object.keys(stats_source_cities).map(function(key) {
    return [key, stats_source_cities[key]];
  });

  var top5_dest = Object.keys(stats_destination_countries).map(function(key) {
    return [key, stats_destination_countries[key]];
  });

  var top5_source_sorted = top5_source.sort(function(a, b) { return a[1]['mean'] > b[1]['mean'] ? 1 : -1; }).slice(0, 5);
  var top5_destination_slow = top5_dest.sort(function(a, b) { return a[1]['mean'] < b[1]['mean'] ? 1 : -1; }).slice(0, 5);
  var top5_destination_fast = top5_dest.sort(function(a, b) { return a[1]['mean'] > b[1]['mean'] ? 1 : -1; }).slice(0, 5);

  return [top5_source_sorted, top5_destination_fast, top5_destination_slow];
}

router.get('/', function(req, res, next) {
  res.render('stats', {
    title: 'Stats',
    stats_destination_cities: stats_destination_cities,
    stats_destination_countries: stats_destination_countries,
    stats_destination_asn: stats_destination_asn,
    stats_source_cities: stats_source_cities,
    stats_source_countries: stats_source_countries,
    stats_source_asn: stats_source_asn
  });
});

module.exports = { 
  router:router,
  getTop5Stats:getTop5Stats
}

