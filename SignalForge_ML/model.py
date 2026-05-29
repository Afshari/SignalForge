# model.py
# LSTM autoencoder architecture for FFT magnitude reconstruction.
# Encoder compresses the input into a latent vector.
# Decoder reconstructs the input from the latent vector.
# Anomaly score = MSE between input and reconstruction.

import torch
import torch.nn as nn

import config


class LSTMEncoder(nn.Module):
    """
    Encodes a sequence of FFT magnitude vectors into a fixed-size latent vector.

    Input shape:  (batch, seq_len, input_size)
    Output shape: (batch, latent_size)
    """

    def __init__(self, input_size: int, hidden_size: int, latent_size: int, num_layers: int):
        super().__init__()
        self.lstm = nn.LSTM(
            input_size=input_size,
            hidden_size=hidden_size,
            num_layers=num_layers,
            batch_first=True,
        )
        # Project final hidden state down to latent dimension
        self.fc = nn.Linear(hidden_size, latent_size)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # x: (batch, seq_len, input_size)
        _, (h_n, _) = self.lstm(x)
        # h_n: (num_layers, batch, hidden_size) - take the last layer
        h_last = h_n[-1]              # (batch, hidden_size)
        latent = self.fc(h_last)      # (batch, latent_size)
        return latent


class LSTMDecoder(nn.Module):
    """
    Reconstructs a sequence of FFT magnitude vectors from a latent vector.

    Input shape:  (batch, latent_size)
    Output shape: (batch, seq_len, input_size)
    """

    def __init__(self, latent_size: int, hidden_size: int, input_size: int, num_layers: int, seq_len: int):
        super().__init__()
        self.seq_len = seq_len
        self.hidden_size = hidden_size
        self.num_layers = num_layers

        # Project latent vector back up to hidden size for LSTM initialization
        self.fc = nn.Linear(latent_size, hidden_size)

        self.lstm = nn.LSTM(
            input_size=latent_size,
            hidden_size=hidden_size,
            num_layers=num_layers,
            batch_first=True,
        )
        # Project LSTM output back to original input size
        self.output_fc = nn.Linear(hidden_size, input_size)

    def forward(self, latent: torch.Tensor) -> torch.Tensor:
        # latent: (batch, latent_size)
        batch_size = latent.size(0)

        # Repeat latent vector across seq_len timesteps as decoder input
        decoder_input = latent.unsqueeze(1).repeat(1, self.seq_len, 1)  # (batch, seq_len, latent_size)

        # Initialize hidden state from latent vector
        h_0 = self.fc(latent)                              # (batch, hidden_size)
        h_0 = h_0.unsqueeze(0).repeat(self.num_layers, 1, 1)  # (num_layers, batch, hidden_size)
        c_0 = torch.zeros_like(h_0)

        output, _ = self.lstm(decoder_input, (h_0, c_0))  # (batch, seq_len, hidden_size)
        reconstruction = self.output_fc(output)            # (batch, seq_len, input_size)
        return reconstruction


class LSTMAutoencoder(nn.Module):
    """
    Full LSTM autoencoder: encoder + decoder.

    Input shape:  (batch, seq_len, input_size)
    Output shape: (batch, seq_len, input_size)

    Use reconstruction_error() at inference time to get per-sample anomaly scores.
    """

    def __init__(
        self,
        input_size: int  = config.INPUT_SIZE,
        hidden_size: int = config.HIDDEN_SIZE,
        latent_size: int = config.LATENT_SIZE,
        num_layers: int  = config.NUM_LAYERS,
        seq_len: int     = 1,
    ):
        super().__init__()
        self.encoder = LSTMEncoder(input_size, hidden_size, latent_size, num_layers)
        self.decoder = LSTMDecoder(latent_size, hidden_size, input_size, num_layers, seq_len)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        latent        = self.encoder(x)   # (batch, latent_size)
        reconstruction = self.decoder(latent)  # (batch, seq_len, input_size)
        return reconstruction

    def reconstruction_error(self, x: torch.Tensor) -> torch.Tensor:
        """
        Compute per-sample MSE between input and reconstruction.

        Args:
            x: Input tensor of shape (batch, seq_len, input_size).

        Returns:
            errors: Tensor of shape (batch,) with one MSE score per sample.
        """
        with torch.no_grad():
            reconstruction = self.forward(x)
            # Mean over seq_len and input_size dimensions
            errors = ((x - reconstruction) ** 2).mean(dim=(1, 2))
        return errors


def build_model(device: torch.device) -> LSTMAutoencoder:
    """Instantiate the model and move it to the target device."""
    model = LSTMAutoencoder()
    model.to(device)
    return model