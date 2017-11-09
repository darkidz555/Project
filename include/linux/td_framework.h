/*
 * Copyright (C) 2013 TripNDroid Mobile Engineering
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __TD_FRAMEWORK_H__
#define __TD_FRAMEWORK_H__

/* cpufreq sleep min/max */
#define TDF_FREQ_SLEEP_MAX	364800
#define TDF_FREQ_SLEEP_MIN	230000

#define TDF_FREQ_MIN		230000
#define TDF_FREQ_IDLE		364800

#define TDF_FREQ_PWRSAVE_MAX	1036800

/* output debug info to kmsg, adds some heavy overhead! */
#ifdef CONFIG_TDF_DEBUG
#define TDF_SUSPEND_DEBUG
#endif

#endif //__TD_FRAMEWORK_H__
