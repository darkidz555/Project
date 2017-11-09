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

#ifndef __TDF_HOTPLUG_H__
#define __TDF_HOTPLUG_H__

/* hotplug states */
enum {
	TDF_HOTPLUG_DISABLED = 0,
	TDF_HOTPLUG_IDLE,
	TDF_HOTPLUG_DOWN,
	TDF_HOTPLUG_UP,
};

/* standard defines first */
#define TDF_HOTPLUG_PAUSE		10000
#define TDF_HOTPLUG_SAMPLE_MS	20000
#define TDF_HOTPLUG_DELAY		130

#endif //__TDF_HOTPLUG_H__
