// ARM11 performance counters, a test tool (pmu.c)
#pragma once

// pmu.c - ARM11 performance counters (test tool)
void pmu_start(int ev0, int ev1);
void pmu_begin(void);
void pmu_end(void);

void pmu_renderer(double values[4]);

void pmu_category_detail(double values[21]);
void pmu_categories(double cycles[3], unsigned counts[3]);
void pmu_report(void);
void pmu_stop(void);
