/*************************************************************************************************************
\file       profile_self_check.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the [profile-check] startup self-check for the frame statistics.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace profile
{
	/*!
		@brief
			Startup check for the frame statistics: exclusive phase times, the
			history ring and the summary.

			Needs no window and no viewer. Prints
			"[profile-check] all checks passed", or one line per failure.
		*/
	void runSelfCheck();
}
