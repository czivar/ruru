-- This is how I set up the asn db with sqlite

CREATE TABLE "ip2proxy" (
	"ip_from" integer UNSIGNED,
	"ip_to" integer UNSIGNED,
	"proxy_type" VARCHAR(3),
	"country_code" CHAR(2),
	"country_name" VARCHAR(64),
	"region_name" VARCHAR(128),
	"city_name" VARCHAR(128),
	"isp" VARCHAR(256)
);

CREATE INDEX idx_ip_from on ip2proxy (ip_from);
CREATE INDEX idx_ip_to on ip2proxy (ip_to);
CREATE UNIQUE INDEX idx_ip_from_to on ip2proxy (ip_from, ip_to);

.separator ","
.import proxy.csv ip2proxy 
