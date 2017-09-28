-- This is how I set up the asn db with sqlite

CREATE TABLE "ip2location_asn" (
	"ip_from" integer UNSIGNED,
	"ip_to" integer UNSIGNED,
	"cidr" VARCHAR(18),
	"asn" VARCHAR(5),
	"as" VARCHAR(256)
);

CREATE INDEX idx_ip_from on ip2location_asn (ip_from);
CREATE INDEX idx_ip_to on ip2location_asn (ip_to);
CREATE UNIQUE INDEX idx_ip_from_to on ip2location_asn (ip_from, ip_to);

.separator ","
.import asn.csv ip2location_asn
