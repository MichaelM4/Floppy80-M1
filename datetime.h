
#define SECONDS_IN_HOUR 3600		// number of seconds in an hour
#define SECONDS_IN_DAY  86400		// number of seconds in a day
#define SECONDS_IN_YEAR 31536000	// 365 * 24 * 60 * 60
#define DAYS_IN_FOUR_YEARS 1461		// 365 * 4 + 1

typedef struct {
    unsigned int sec;
	unsigned int min;
	unsigned int hour;
	unsigned int day;
	unsigned int month;
	unsigned int year;
} CodedDateTime;

void CodeDateTime(unsigned long datetime, CodedDateTime* pdt);
void ParseDateTime(char* psz, CodedDateTime* pdt);
unsigned long EncodeDateTime(CodedDateTime* pdt);
