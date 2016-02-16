enum time_unit { SECONDS, HOURS, MINUTES, DAYS, MONTHS, YEARS };

advance_time(struct timespec* base, time_unit unit, uint32_t increment);
