// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_AUTOFILL_CARD_UNMASK_PROMPT_CONTROLLER_IMPL_H_
#define CHROME_BROWSER_UI_AUTOFILL_CARD_UNMASK_PROMPT_CONTROLLER_IMPL_H_

#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/autofill/card_unmask_prompt_controller.h"
#include "components/autofill/core/browser/credit_card.h"

namespace autofill {

class CardUnmaskDelegate;
class CardUnmaskPromptView;

class CardUnmaskPromptControllerImpl : public CardUnmaskPromptController {
 public:
  explicit CardUnmaskPromptControllerImpl(content::WebContents* web_contents);
  ~CardUnmaskPromptControllerImpl();

  // Functions called by ChromeAutofillClient.
  void ShowPrompt(const CreditCard& card,
                  base::WeakPtr<CardUnmaskDelegate> delegate);
  // The CVC the user entered went through validation.
  void OnVerificationResult(bool success);

  // CardUnmaskPromptController implementation.
  void OnUnmaskDialogClosed() override;
  void OnUnmaskResponse(const base::string16& cvc) override;
  content::WebContents* GetWebContents() override;
  base::string16 GetWindowTitle() const override;
  base::string16 GetInstructionsMessage() const override;
  int GetCvcImageRid() const override;
  bool InputTextIsValid(const base::string16& input_text) const override;

 private:
  content::WebContents* web_contents_;
  CreditCard card_;
  base::WeakPtr<CardUnmaskDelegate> delegate_;
  CardUnmaskPromptView* card_unmask_view_;

  base::WeakPtrFactory<CardUnmaskPromptControllerImpl> weak_pointer_factory_;

  DISALLOW_COPY_AND_ASSIGN(CardUnmaskPromptControllerImpl);
};

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_AUTOFILL_CARD_UNMASK_PROMPT_CONTROLLER_IMPL_H_
