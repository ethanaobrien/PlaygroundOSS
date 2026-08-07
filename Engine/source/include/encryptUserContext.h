/* 
   Copyright 2013 KLab Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

struct SUserStruct {
	u32	m_streamOffset;
	u32	m_blockRemaining;
	u32	m_payloadLength;
	u8	m_primaryKeyBytes[4];
	u8	m_secondaryKeyBytes[4];
	u8	m_headerKeyBytes[4];
	u8	m_workKeyBytes[4];
	u8	m_savedWorkKeyBytes[4];
	u8	m_streamPhase;
	u32	m_primaryInitialState;
	u32	m_primaryVariant;
	u32	m_primaryState;
	u32	m_secondaryInitialState;
	u32	m_secondaryState;
};
